// BLE HID (keyboard + mouse + consumer) for Musecorder — see musecorder_hid.h.
// Persistent HID mode: EITHER a Muse gadget OR a BLE HID device, chosen by an
// NVS flag; toggling reboots. On a HID-mode boot app_run skips Wi-Fi/Muse and
// calls musecorder_hid_begin(), which advertises as a keyboard/mouse that stays
// discoverable. The keyboard send path is proven first; mouse/consumer helpers
// and the applet command surface (macropads) build on the same service.
#include "musecorder_hid.h"
#include "sdkconfig.h"

#if CONFIG_HOMEHUB_MUSECORDER

#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "ble_server.h"   // ble_server_stop_advertising()

static const char *TAG = "mc_hid";

#define HID_APPEARANCE_KEYBOARD 0x03C1
#define RPT_ID_KBD      1
#define RPT_ID_MOUSE    2
#define RPT_ID_CONSUMER 3

#define HID_NVS_NS  "mc_hid"
#define HID_NVS_KEY "on"

// ---- Composite report descriptor (keyboard + mouse + consumer) -------------
static const uint8_t s_report_map[] = {
    // Keyboard (Report ID 1): [modifier, reserved, key0..key5]
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, RPT_ID_KBD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    // Mouse (Report ID 2): [buttons, dx, dy, wheel], relative
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, RPT_ID_MOUSE,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
    // Consumer control (Report ID 3): one 16-bit usage
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, RPT_ID_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

static uint8_t s_kbd[8]      = {0};
static uint8_t s_mouse[4]    = {0};
static uint8_t s_consumer[2] = {0};
static uint16_t s_h_kbd, s_h_mouse, s_h_consumer, s_h_batt;

static const uint8_t s_hid_info[4] = {0x11, 0x01, 0x00, 0x02};
static const uint8_t s_pnp[7]      = {0x02, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01};
static uint8_t s_proto_mode = 0x01;
static uint8_t s_batt_level = 100;

static const uint8_t s_rr_kbd[2]      = {RPT_ID_KBD, 0x01};
static const uint8_t s_rr_mouse[2]    = {RPT_ID_MOUSE, 0x01};
static const uint8_t s_rr_consumer[2] = {RPT_ID_CONSUMER, 0x01};

typedef struct { const uint8_t *buf; uint16_t len; } rpt_src_t;
static const rpt_src_t s_src_kbd      = { s_kbd, sizeof s_kbd };
static const rpt_src_t s_src_mouse    = { s_mouse, sizeof s_mouse };
static const rpt_src_t s_src_consumer = { s_consumer, sizeof s_consumer };

// ---- GATT access callbacks -------------------------------------------------
static int ac_ro(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a;
    const uint8_t *p = (const uint8_t *)arg;
    size_t n = 0;
    if (p == s_hid_info) n = sizeof s_hid_info;
    else if (p == s_pnp) n = sizeof s_pnp;
    else if (p == s_report_map) n = sizeof s_report_map;
    else if (p == &s_proto_mode) n = 1;
    else if (p == &s_batt_level) n = 1;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR && ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, p, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int ac_report(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    const rpt_src_t *s = (const rpt_src_t *)arg;
    return os_mbuf_append(ctxt->om, s->buf, s->len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int ac_report_ref(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, (const uint8_t *)arg, 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int ac_proto_mode(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return os_mbuf_append(ctxt->om, &s_proto_mode, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t n = 0; ble_hs_mbuf_to_flat(ctxt->om, &s_proto_mode, 1, &n); return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int ac_ctrl_pt(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a; (void)arg; (void)ctxt; return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),   // HID
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = ac_proto_mode,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = ac_ro, .arg = (void *)s_report_map,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = ac_ro, .arg = (void *)s_hid_info,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = ac_ctrl_pt,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = ac_report, .arg = (void *)&s_src_kbd,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_h_kbd,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = ac_report_ref, .arg = (void *)s_rr_kbd }, { 0 } } },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = ac_report, .arg = (void *)&s_src_mouse,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_h_mouse,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = ac_report_ref, .arg = (void *)s_rr_mouse }, { 0 } } },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = ac_report, .arg = (void *)&s_src_consumer,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_h_consumer,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = ac_report_ref, .arg = (void *)s_rr_consumer }, { 0 } } },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),   // Battery
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = ac_ro, .arg = (void *)&s_batt_level,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_h_batt },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),   // Device Information
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = ac_ro, .arg = (void *)s_pnp,
              .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    { 0 },
};

const struct ble_gatt_svc_def *musecorder_hid_svcs(void) { return s_svcs; }

// ---- Mode flag (NVS) -------------------------------------------------------
static int s_mode = -1;   // -1 = unread, 0/1 = cached

bool musecorder_hid_mode_active(void) {
    if (s_mode < 0) {
        s_mode = 0;
        nvs_handle_t h;
        if (nvs_open(HID_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            uint8_t v = 0;
            if (nvs_get_u8(h, HID_NVS_KEY, &v) == ESP_OK) s_mode = v ? 1 : 0;
            nvs_close(h);
        }
    }
    return s_mode == 1;
}

void musecorder_hid_set_mode(bool on) {
    nvs_handle_t h;
    if (nvs_open(HID_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, HID_NVS_KEY, on ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "HID mode -> %d; rebooting", (int)on);
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

// ---- Connection state ------------------------------------------------------
static volatile bool s_connected = false, s_enc = false, s_subscribed = false;
static volatile bool s_typed_test = false;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static bool s_began = false;

bool musecorder_hid_ready(void) { return s_connected && s_enc && s_subscribed; }

// ---- ASCII -> HID keycode --------------------------------------------------
static bool ascii_to_hid(char ch, uint8_t *mod, uint8_t *key) {
    *mod = 0; *key = 0;
    if (ch >= 'a' && ch <= 'z') { *key = 0x04 + (ch - 'a'); return true; }
    if (ch >= 'A' && ch <= 'Z') { *mod = 0x02; *key = 0x04 + (ch - 'A'); return true; }
    if (ch >= '1' && ch <= '9') { *key = 0x1E + (ch - '1'); return true; }
    switch (ch) {
        case '0': *key = 0x27; return true;
        case ' ': *key = 0x2C; return true;
        case '\n': *key = 0x28; return true;
        case '.': *key = 0x37; return true;
        case ',': *key = 0x36; return true;
        case '-': *key = 0x2D; return true;
        case '!': *mod = 0x02; *key = 0x1E; return true;
        default: return false;
    }
}

static int hid_notify(uint16_t handle, const uint8_t *data, uint16_t len) {
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || handle == 0) return -1;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return -1;
    return ble_gatts_notify_custom(s_conn, handle, om);
}

int musecorder_hid_type(const char *s) {
    if (!musecorder_hid_ready() || !s) return 0;
    int n = 0;
    for (const char *p = s; *p; p++) {
        uint8_t mod, key;
        if (!ascii_to_hid(*p, &mod, &key)) continue;
        s_kbd[0] = mod; s_kbd[2] = key;
        hid_notify(s_h_kbd, s_kbd, sizeof s_kbd);
        vTaskDelay(pdMS_TO_TICKS(12));
        memset(s_kbd, 0, sizeof s_kbd);
        hid_notify(s_h_kbd, s_kbd, sizeof s_kbd);
        vTaskDelay(pdMS_TO_TICKS(12));
        n++;
    }
    return n;
}

// One-shot "it works" proof, typed once per HID-mode boot when a host is ready.
static void hid_test_type_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));   // let the host settle after subscribe
    musecorder_hid_type("hi from musecorder\n");
    vTaskDelete(NULL);
}

static int hid_advertise(void);

static void maybe_type_test(void) {
    if (!s_typed_test && musecorder_hid_ready()) {
        s_typed_test = true;
        xTaskCreate(hid_test_type_task, "mc_hid_t", 4096, NULL, 5, NULL);
    }
}

// ---- GAP event handler -----------------------------------------------------
static int hid_gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle; s_connected = true;
            ESP_LOGI(TAG, "host connected (conn=%d); initiating security", s_conn);
            ble_gap_security_initiate(s_conn);
        } else {
            ESP_LOGW(TAG, "connect failed status=%d; re-advertising", event->connect.status);
            hid_advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "host disconnected (reason=0x%x); re-advertising", event->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE; s_connected = false; s_enc = false; s_subscribed = false;
        hid_advertise();   // stay discoverable
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        s_enc = (event->enc_change.status == 0);
        ESP_LOGI(TAG, "enc change status=%d enc=%d", event->enc_change.status, (int)s_enc);
        maybe_type_test();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_h_kbd) {
            s_subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "keyboard subscribe notify=%d", (int)s_subscribed);
            maybe_type_test();
        }
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io io = { .action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 1 };
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        } else if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = { .action = BLE_SM_IOACT_DISP, .passkey = 123456 };
            ble_sm_inject_io(event->passkey.conn_handle, &io);
            ESP_LOGW(TAG, "if the host asks for a PIN, enter: 123456");
        } else {
            ESP_LOGW(TAG, "unsupported passkey action %d", event->passkey.params.action);
        }
        return 0;
    default:
        return 0;
    }
}

static int hid_advertise(void) {
    uint8_t own_addr_type;
    ble_hs_util_ensure_addr(0);
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc) { ESP_LOGW(TAG, "infer_auto rc=%d", rc); return rc; }

    struct ble_hs_adv_fields f; memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance = HID_APPEARANCE_KEYBOARD; f.appearance_is_present = 1;
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    f.uuids16 = (ble_uuid16_t *)&hid_uuid; f.num_uuids16 = 1; f.uuids16_is_complete = 1;
    rc = ble_gap_adv_set_fields(&f);
    if (rc) { ESP_LOGW(TAG, "adv_set_fields rc=%d", rc); return rc; }

    struct ble_hs_adv_fields rsp; memset(&rsp, 0, sizeof rsp);
    const char *name = ble_svc_gap_device_name();
    rsp.name = (uint8_t *)name; rsp.name_len = name ? strlen(name) : 0; rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params advp; memset(&advp, 0, sizeof advp);
    advp.conn_mode = BLE_GAP_CONN_MODE_UND; advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &advp, hid_gap_event, NULL);
    if (rc) ESP_LOGW(TAG, "adv_start rc=%d", rc);
    return rc;
}

void musecorder_hid_begin(void) {
    if (s_began) return;
    s_began = true;
    ble_server_stop_advertising(false);   // drop the SDK's setup advertising; we own the radio now
    vTaskDelay(pdMS_TO_TICKS(150));
    if (hid_advertise() == 0)
        ESP_LOGI(TAG, "HID mode: advertising as a BLE keyboard/mouse (persistent)");
    else
        ESP_LOGE(TAG, "HID mode: advertise failed");
}

#else   // !CONFIG_HOMEHUB_MUSECORDER
const struct ble_gatt_svc_def *musecorder_hid_svcs(void) { return 0; }
bool musecorder_hid_mode_active(void) { return false; }
void musecorder_hid_set_mode(bool on) { (void)on; }
void musecorder_hid_begin(void) {}
bool musecorder_hid_ready(void) { return false; }
int musecorder_hid_type(const char *s) { (void)s; return 0; }
#endif
