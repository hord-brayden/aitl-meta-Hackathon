/**
 * Musecorder → Google Sheets webhook.
 *
 * 1. Create a Google Sheet. Extensions ▸ Apps Script. Paste this file, save.
 * 2. Deploy ▸ New deployment ▸ type "Web app" ▸ Execute as: Me ▸ Who has access: Anyone ▸ Deploy.
 *    Copy the Web app URL (https://script.google.com/macros/s/.../exec).
 * 3. Put that URL in the applet's `state.sheet` (examples/applets/door-tally.json) or tell Muse the URL.
 *
 * Every POST appends one row: timestamp, then every field of the JSON body in a stable column order.
 * GET returns the row count so an applet can show "server total".
 */
const SHEET_NAME = "tally";

function sheet_() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sh = ss.getSheetByName(SHEET_NAME);
  if (!sh) { sh = ss.insertSheet(SHEET_NAME); sh.appendRow(["time", "device", "door", "count"]); }
  return sh;
}

function doPost(e) {
  let body = {};
  try { body = JSON.parse(e.postData.contents || "{}"); } catch (err) { body = { raw: e.postData && e.postData.contents }; }
  const sh = sheet_();
  const header = sh.getRange(1, 1, 1, Math.max(sh.getLastColumn(), 1)).getValues()[0].filter(String);
  const row = [new Date()];
  for (const col of header.slice(1)) row.push(body[col] !== undefined ? body[col] : "");
  for (const k of Object.keys(body)) if (header.indexOf(k) < 0) { header.push(k); sh.getRange(1, header.length).setValue(k); row.push(body[k]); }
  sh.appendRow(row);
  const out = { ok: true, rows: sh.getLastRow() - 1, total: sumColumn_(sh, "count") };
  return ContentService.createTextOutput(JSON.stringify(out)).setMimeType(ContentService.MimeType.JSON);
}

function doGet() {
  const sh = sheet_();
  return ContentService.createTextOutput(JSON.stringify({ ok: true, rows: sh.getLastRow() - 1, total: sumColumn_(sh, "count") }))
    .setMimeType(ContentService.MimeType.JSON);
}

function sumColumn_(sh, name) {
  const header = sh.getRange(1, 1, 1, sh.getLastColumn()).getValues()[0];
  const i = header.indexOf(name);
  if (i < 0 || sh.getLastRow() < 2) return 0;
  return sh.getRange(2, i + 1, sh.getLastRow() - 1, 1).getValues().reduce((a, r) => a + (Number(r[0]) || 0), 0);
}
