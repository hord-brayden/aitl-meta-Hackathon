#include "expr.h"
#include <math.h>
#include <ArduinoJson.h>

namespace expr {

bool Value::truthy() const {
  switch (t) { case Null: return false; case Num: case Bool: return n != 0; case Str: return s.length() > 0 && s != "false" && s != "0"; }
  return false;
}
double Value::toNum() const {
  switch (t) { case Null: return 0; case Num: case Bool: return n; case Str: { if (!s.length()) return 0; char* e; double v = strtod(s.c_str(), &e); return e == s.c_str() ? 0 : v; } }
  return 0;
}
String fmtNum(double v, int decimals) {
  if (isnan(v) || isinf(v)) return "0";
  char b[40];
  if (decimals >= 0) { snprintf(b, sizeof(b), "%.*f", decimals > 9 ? 9 : decimals, v); return String(b); }
  if (fabs(v) < 1e15 && v == floor(v)) { snprintf(b, sizeof(b), "%lld", (long long)v); return String(b); }
  snprintf(b, sizeof(b), "%.6f", v);
  String s(b); while (s.endsWith("0")) s.remove(s.length() - 1); if (s.endsWith(".")) s.remove(s.length() - 1);
  return s;
}
String Value::toStr() const {
  switch (t) { case Null: return ""; case Num: return fmtNum(n); case Bool: return n ? "true" : "false"; case Str: return s; }
  return "";
}

struct P {
  const char* s; size_t i; Resolver r; void* ctx; String err; int depth;
  char peek() const { return s[i]; }
  char peek2() const { return s[i] ? s[i + 1] : 0; }
  void ws() { while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++; }
  bool take(const char* tok) { ws(); size_t n = strlen(tok); if (!strncmp(s + i, tok, n)) { i += n; return true; } return false; }
  void fail(const char* m) { if (!err.length()) { err = m; err += " at "; err += (int)i; } }
};

static Value parseTernary(P& p);

static bool isIdStart(char c) { return isalpha((unsigned char)c) || c == '_'; }
static bool isIdChar(char c) { return isalnum((unsigned char)c) || c == '_' || c == '.'; }

static Value callFn(P& p, const String& name, std::vector<Value>& a) {
  auto A = [&](size_t k) -> Value { return k < a.size() ? a[k] : Value(); };
  auto N = [&](size_t k) -> double { return A(k).toNum(); };
  auto S = [&](size_t k) -> String { return A(k).toStr(); };
  if (name == "len") return Value::num(S(0).length());
  if (name == "str") return Value::str(S(0));
  if (name == "num") return Value::num(N(0));
  if (name == "int") return Value::num((double)(long long)N(0));
  if (name == "round") { int d = a.size() > 1 ? (int)N(1) : 0; double m = pow(10, d); return Value::num(round(N(0) * m) / m); }
  if (name == "abs") return Value::num(fabs(N(0)));
  if (name == "floor") return Value::num(floor(N(0)));
  if (name == "ceil") return Value::num(ceil(N(0)));
  if (name == "min") { double m = N(0); for (size_t k = 1; k < a.size(); k++) m = fmin(m, N(k)); return Value::num(m); }
  if (name == "max") { double m = N(0); for (size_t k = 1; k < a.size(); k++) m = fmax(m, N(k)); return Value::num(m); }
  if (name == "clamp") return Value::num(fmin(fmax(N(0), N(1)), N(2)));
  if (name == "fmt") return Value::str(fmtNum(N(0), a.size() > 1 ? (int)N(1) : 0));
  if (name == "pad") { String s = S(0); int w = (int)N(1); char c = a.size() > 2 && S(2).length() ? S(2)[0] : '0'; while ((int)s.length() < w) s = String(c) + s; return Value::str(s); }
  if (name == "upper") { String s = S(0); s.toUpperCase(); return Value::str(s); }
  if (name == "lower") { String s = S(0); s.toLowerCase(); return Value::str(s); }
  if (name == "trim") { String s = S(0); s.trim(); return Value::str(s); }
  if (name == "contains") return Value::boolean(S(0).indexOf(S(1)) >= 0);
  if (name == "startswith") return Value::boolean(S(0).startsWith(S(1)));
  if (name == "endswith") return Value::boolean(S(0).endsWith(S(1)));
  if (name == "replace") { String s = S(0); s.replace(S(1), S(2)); return Value::str(s); }
  if (name == "substr") { String s = S(0); int st = (int)N(1); int ln = a.size() > 2 ? (int)N(2) : (int)s.length(); if (st < 0) st = 0; if (st > (int)s.length()) st = s.length(); return Value::str(s.substring(st, st + ln)); }
  if (name == "random") { long lo = (long)N(0), hi = (long)N(1); if (hi < lo) { long t = lo; lo = hi; hi = t; } return Value::num(lo + (long)(esp_random() % (uint32_t)(hi - lo + 1))); }
  if (name == "hhmmss") { long t = (long)N(0); if (t < 0) t = 0; char b[20]; if (t >= 3600) snprintf(b, sizeof(b), "%ld:%02ld:%02ld", t / 3600, (t % 3600) / 60, t % 60); else snprintf(b, sizeof(b), "%02ld:%02ld", t / 60, t % 60); return Value::str(b); }
  if (name == "json") {   // json(text, "a.b.0.c")
    JsonDocument d; if (deserializeJson(d, S(0)) != DeserializationError::Ok) return Value();
    JsonVariant v = d.as<JsonVariant>(); String path = S(1); int st = 0;
    while (st <= (int)path.length() && !v.isNull()) {
      int dot = path.indexOf('.', st); String k = dot < 0 ? path.substring(st) : path.substring(st, dot);
      if (k.length()) { if (v.is<JsonArray>()) v = v[k.toInt()]; else v = v[k]; }
      if (dot < 0) break; st = dot + 1;
    }
    if (v.isNull()) return Value();
    if (v.is<bool>()) return Value::boolean(v.as<bool>());
    if (v.is<double>()) return Value::num(v.as<double>());
    if (v.is<const char*>()) return Value::str(v.as<const char*>());
    String out; serializeJson(v, out); return Value::str(out);
  }
  p.fail("unknown function"); return Value();
}

static Value parsePrimary(P& p) {
  p.ws();
  char c = p.peek();
  if (!c) { p.fail("unexpected end"); return Value(); }
  if (c == '(') { p.i++; Value v = parseTernary(p); if (!p.take(")")) p.fail("expected )"); return v; }
  if (c == '"' || c == '\'') {
    char q = c; p.i++; String out;
    while (p.peek() && p.peek() != q) { if (p.peek() == '\\' && p.peek2()) { p.i++; char e = p.peek(); out += e == 'n' ? '\n' : e == 't' ? '\t' : e; } else out += p.peek(); p.i++; }
    if (p.peek() == q) p.i++; else p.fail("unterminated string");
    return Value::str(out);
  }
  if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p.peek2()))) { char* e; double v = strtod(p.s + p.i, &e); p.i = e - p.s; return Value::num(v); }
  if (isIdStart(c)) {
    size_t st = p.i; while (isIdChar(p.peek())) p.i++;
    String name(p.s + st, p.i - st);
    if (name == "true") return Value::boolean(true);
    if (name == "false") return Value::boolean(false);
    if (name == "null") return Value();
    p.ws();
    if (p.peek() == '(') {
      p.i++; std::vector<Value> args;
      p.ws();
      if (p.peek() != ')') { do { args.push_back(parseTernary(p)); } while (p.take(",")); }
      if (!p.take(")")) p.fail("expected )");
      return callFn(p, name, args);
    }
    Value v; if (p.r && p.r(name, v, p.ctx)) return v;
    return Value();   // unknown names are null
  }
  p.fail("unexpected char"); p.i++; return Value();
}
static Value parseUnary(P& p) {
  p.ws();
  if (p.peek() == '!') { p.i++; return Value::boolean(!parseUnary(p).truthy()); }
  if (p.peek() == '-') { p.i++; return Value::num(-parseUnary(p).toNum()); }
  if (p.peek() == '+') { p.i++; return Value::num(parseUnary(p).toNum()); }
  return parsePrimary(p);
}
static Value parseMul(P& p) {
  Value l = parseUnary(p);
  for (;;) {
    p.ws(); char c = p.peek();
    if (c == '*') { p.i++; l = Value::num(l.toNum() * parseUnary(p).toNum()); }
    else if (c == '/') { p.i++; double d = parseUnary(p).toNum(); l = Value::num(d == 0 ? 0 : l.toNum() / d); }
    else if (c == '%') { p.i++; double d = parseUnary(p).toNum(); l = Value::num(d == 0 ? 0 : fmod(l.toNum(), d)); }
    else return l;
  }
}
static Value parseAdd(P& p) {
  Value l = parseMul(p);
  for (;;) {
    p.ws(); char c = p.peek();
    if (c == '+') { p.i++; Value r = parseMul(p); if (l.t == Value::Str || r.t == Value::Str) l = Value::str(l.toStr() + r.toStr()); else l = Value::num(l.toNum() + r.toNum()); }
    else if (c == '-') { p.i++; l = Value::num(l.toNum() - parseMul(p).toNum()); }
    else return l;
  }
}
static Value parseCmp(P& p) {
  Value l = parseAdd(p);
  for (;;) {
    p.ws();
    if (p.take("<=")) l = Value::boolean(l.toNum() <= parseAdd(p).toNum());
    else if (p.take(">=")) l = Value::boolean(l.toNum() >= parseAdd(p).toNum());
    else if (p.peek() == '<') { p.i++; l = Value::boolean(l.toNum() < parseAdd(p).toNum()); }
    else if (p.peek() == '>') { p.i++; l = Value::boolean(l.toNum() > parseAdd(p).toNum()); }
    else return l;
  }
}
static bool valEq(const Value& a, const Value& b) {
  if (a.t == Value::Str || b.t == Value::Str) return a.toStr() == b.toStr();
  if (a.t == Value::Null || b.t == Value::Null) return a.t == b.t;
  return a.toNum() == b.toNum();
}
static Value parseEq(P& p) {
  Value l = parseCmp(p);
  for (;;) {
    p.ws();
    if (p.take("==")) l = Value::boolean(valEq(l, parseCmp(p)));
    else if (p.take("!=")) l = Value::boolean(!valEq(l, parseCmp(p)));
    else return l;
  }
}
static Value parseAnd(P& p) { Value l = parseEq(p); while (p.take("&&")) { Value r = parseEq(p); l = Value::boolean(l.truthy() && r.truthy()); } return l; }
static Value parseOr(P& p) { Value l = parseAnd(p); while (p.take("||")) { Value r = parseAnd(p); l = Value::boolean(l.truthy() || r.truthy()); } return l; }
static Value parseTernary(P& p) {
  if (++p.depth > 24) { p.fail("too deep"); p.depth--; return Value(); }
  Value c = parseOr(p);
  p.ws();
  if (p.peek() == '?') { p.i++; Value a = parseTernary(p); if (!p.take(":")) p.fail("expected :"); Value b = parseTernary(p); p.depth--; return c.truthy() ? a : b; }
  p.depth--; return c;
}

Value eval(const char* src, Resolver r, void* ctx, String* err) {
  P p{src ? src : "", 0, r, ctx, String(), 0};
  Value v = parseTernary(p);
  p.ws(); if (p.peek() && !p.err.length()) p.fail("trailing input");
  if (err) *err = p.err;
  return p.err.length() ? Value() : v;
}

bool hasTemplate(const char* tpl) { return tpl && strstr(tpl, "${") != nullptr; }

// returns index after the matching '}' of a "${" starting at `start`, or -1
static int findClose(const char* t, int start) {
  int depth = 0;
  for (int i = start; t[i]; i++) {
    if (t[i] == '{') depth++;
    else if (t[i] == '}') { depth--; if (depth == 0) return i + 1; }
    else if (t[i] == '"' || t[i] == '\'') { char q = t[i++]; while (t[i] && t[i] != q) { if (t[i] == '\\' && t[i + 1]) i++; i++; } if (!t[i]) return -1; }
  }
  return -1;
}

Value renderTyped(const char* tpl, Resolver r, void* ctx, String* err) {
  if (!tpl) return Value::str("");
  if (!hasTemplate(tpl)) return Value::str(tpl);
  const char* d = strstr(tpl, "${");
  int close = findClose(tpl, d - tpl + 1);
  if (d == tpl && close > 0 && tpl[close] == 0) {   // exactly one expression: keep the type
    String inner(tpl + 2, close - 3);
    String e; Value v = eval(inner.c_str(), r, ctx, &e);
    if (err && e.length()) *err = e;
    return v;
  }
  return Value::str(render(tpl, r, ctx, err));
}

String render(const char* tpl, Resolver r, void* ctx, String* err) {
  if (!tpl) return "";
  String out; const char* t = tpl; int i = 0;
  while (t[i]) {
    if (t[i] == '$' && t[i + 1] == '{') {
      int close = findClose(t, i + 1);
      if (close < 0) { out += (t + i); break; }
      String inner(t + i + 2, close - i - 3);
      String e; Value v = eval(inner.c_str(), r, ctx, &e);
      if (e.length()) { out += '?'; if (err && !err->length()) *err = e; } else out += v.toStr();
      i = close;
    } else out += t[i++];
  }
  return out;
}
}  // namespace expr
