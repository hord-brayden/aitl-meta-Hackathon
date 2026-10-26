#pragma once
#include <Arduino.h>
#include <vector>

// Application state + bridge protocol handling. The UI observes this through setUiCb().
namespace app {
  struct Model  { String id, label, provider; bool ok; };
  struct Chat   { String id, title; };
  struct Action { String id, label; };
  struct CcSession { String id, title, project, age; };
  struct Alert { String id, kind, title, text, project, session; bool twoWay; };
  struct Msg { uint8_t role; String text; };   // role: 0 user, 1 assistant, 2 error
  struct NowPlaying { String app, state, title, artist; bool playing() const { return state == "playing"; } };
  enum class Ev : uint8_t { Transcript, Lists, Status, Link, Toast, Error, Pairing, Provisioned, Alert, AlertClear, NowPlaying, Inbox };
  typedef void (*UiCb)(Ev ev, const char* arg);

  extern std::vector<Model>  models;
  extern std::vector<Chat>   chats;
  extern std::vector<Action> actions;
  extern std::vector<CcSession> ccSessions;
  extern Alert alert;              // current alert (empty id when none)
  extern std::vector<Alert> inbox;  // recent cards, newest first (max 20)
  extern uint8_t inboxUnread;
  extern NowPlaying np;
  extern String currentModel, currentChatId, currentChatTitle, bridgeName, bridgeVersion, frontApp;
  extern bool dictating, streaming, hasReply, capturing;   // capturing = device mic is recording
  extern String pairFingerprint, pairCode, pairName;

  void begin();
  void tick();
  void setUiCb(UiCb cb);

  const std::vector<Msg>& messages();
  bool transcriptEmpty();
  void clearTranscript();

  void sendPrompt(const String& text);
  void stopGeneration();
  void newChat();
  void openChat(const String& id);
  void deleteChat(const String& id);
  void setModel(const String& id);
  void runAction(const String& id);
  void refreshLists();
  void listSessions();              // ask the bridge for recent Claude Code sessions
  void openSession(const String& id);
  void replyAlert(const char* action);  // "allow" | "deny" | "dismiss"
  void inboxMarkRead();
  void inboxClear();

  void typeToMac(const String& text);   // honours g_settings.hidTarget
  void keyToMac(const char* combo);
  void dictation(const char* op);       // "start" | "stop" | "toggle" — routes to host Mac or device mic per settings
  void captureStart(); void captureStop(bool discard = false);
  void pasteLastReply();
  void copyLastReply();
  void mediaKey(uint16_t usage);

  void startNetworkPairing();
  void confirmPairing(bool accept);
  void forgetBridge();

  String deviceId();
  String linkSummary();                 // "USB" | "WIFI" | "--"
  bool bridgeLinked();
}
