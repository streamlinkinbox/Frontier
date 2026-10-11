#!/usr/bin/env python3
"""Charge backend v1: Xsolla webhook receiver + premium/entitlement service.

Stdlib only -- no pip install needed. SQLite storage. See Backend/README.md.

Endpoints:
  GET  /healthz             liveness probe, no auth
  POST /v1/register         game client announces an Epic login
  GET  /v1/premium          game client asks "is this Epic account premium?"
  POST /webhooks/xsolla     Xsolla order_paid / order_canceled / user_validation

Config (environment):
  XSOLLA_SECRET_KEY         webhook signing secret from Publisher Account
  XSOLLA_REQUIRE_SIGNATURE  "1" (default) or "0" to skip verification (local tests only)
  CHARGE_API_KEY            if set, /v1/* requires header X-Charge-Key
  PORT                      listen port, default 8080
  DB_PATH                   sqlite file, default charge.db
"""

import hashlib
import hmac
import json
import os
import sqlite3
import sys
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# SKU -> (premium_grant, autocoin_per_unit). premium_grant is None or "lifetime".
SKU_CATALOG = {
    "premium_pass": ("lifetime", 0),
    "entry_tokens": (None, 100),
    "podium_tokens": (None, 550),
    "championship_tokens": (None, 1200),
}

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
  epic_id TEXT PRIMARY KEY,
  display_name TEXT DEFAULT '',
  premium TEXT DEFAULT 'none',
  premium_order_id TEXT DEFAULT '',
  autocoin INTEGER DEFAULT 0,
  created_utc TEXT DEFAULT '',
  updated_utc TEXT DEFAULT ''
);
CREATE TABLE IF NOT EXISTS processed (
  notification_type TEXT,
  order_id TEXT,
  received_utc TEXT DEFAULT '',
  PRIMARY KEY (notification_type, order_id)
);
CREATE TABLE IF NOT EXISTS grants (
  id INTEGER PRIMARY KEY,
  order_id TEXT,
  sku TEXT,
  quantity INTEGER,
  coins INTEGER,
  premium TEXT,
  granted_utc TEXT DEFAULT ''
);
"""


def utcnow():
    return datetime.now(timezone.utc).isoformat()


def log(*parts):
    sys.stderr.write("%s %s\n" % (utcnow(), " ".join(str(p) for p in parts)))
    sys.stderr.flush()


# ---------------------------------------------------------------- config

class Config:
    def __init__(self, secret="", require_signature=True, charge_api_key="",
                 port=8080, db_path="charge.db"):
        self.secret = secret
        self.require_signature = require_signature
        self.charge_api_key = charge_api_key
        self.port = port
        self.db_path = db_path

    @classmethod
    def from_env(cls):
        return cls(
            secret=os.environ.get("XSOLLA_SECRET_KEY", ""),
            require_signature=os.environ.get("XSOLLA_REQUIRE_SIGNATURE", "1") != "0",
            charge_api_key=os.environ.get("CHARGE_API_KEY", ""),
            port=int(os.environ.get("PORT", "8080")),
            db_path=os.environ.get("DB_PATH", "charge.db"),
        )


# ---------------------------------------------------------------- signature
# Xsolla: Authorization: Signature <sha1(raw_body + secret_key) lowercase hex>

def verify_signature(raw_body: bytes, auth_header: str, secret: str) -> bool:
    if not auth_header or not secret:
        return False
    provided = auth_header.strip()
    if provided.lower().startswith("signature "):
        provided = provided[len("signature "):]
    provided = provided.strip().lower()
    expected = hashlib.sha1(raw_body + secret.encode("utf-8")).hexdigest()
    return hmac.compare_digest(expected, provided)


# ---------------------------------------------------------------- payload parsing (defensive: Xsolla has several shapes)

def _as_str(value):
    if value is None:
        return ""
    if isinstance(value, dict):
        return _as_str(value.get("value"))
    return str(value)


def extract_user_id(payload):
    user = payload.get("user") or {}
    for candidate in (_as_str(user.get("id")), _as_str(user.get("external_id"))):
        if candidate:
            return candidate
    custom = payload.get("custom_parameters") or {}
    for key in ("epic_account_id", "epic_id", "user_id"):
        if custom.get(key):
            return str(custom[key])
    return ""


def extract_order_id(payload):
    order = payload.get("order") or {}
    for candidate in (order.get("id"), payload.get("order_id"), payload.get("id")):
        if candidate is not None and str(candidate) != "":
            return str(candidate)
    txn = payload.get("transaction") or {}
    if txn.get("id") is not None and str(txn.get("id")) != "":
        return str(txn["id"])
    return ""


def extract_items(payload):
    items = payload.get("items")
    if not items:
        purchase = payload.get("purchase") or {}
        items = purchase.get("items") or []
    out = []
    for it in items:
        if not isinstance(it, dict):
            continue
        if it.get("is_bundle_content"):  # parent bundle carries the grant
            continue
        sku = str(it.get("sku", "") or "")
        try:
            qty = int(it.get("quantity", 1) or 1)
        except (TypeError, ValueError):
            qty = 1
        if sku:
            out.append((sku, max(qty, 0)))
    return out


# ---------------------------------------------------------------- storage

class ChargeDB:
    def __init__(self, path):
        self.path = path
        with self._connect() as conn:
            conn.executescript(SCHEMA)

    def _connect(self):
        return sqlite3.connect(self.path, timeout=10)

    def ensure_user(self, epic_id, display_name=None):
        now = utcnow()
        with self._connect() as conn:
            conn.execute(
                "INSERT INTO users (epic_id, created_utc, updated_utc)"
                " VALUES (?, ?, ?) ON CONFLICT(epic_id) DO NOTHING",
                (epic_id, now, now))
            if display_name:
                conn.execute(
                    "UPDATE users SET display_name = ?, updated_utc = ?"
                    " WHERE epic_id = ?",
                    (display_name, now, epic_id))

    def get_user(self, epic_id):
        with self._connect() as conn:
            conn.row_factory = sqlite3.Row
            row = conn.execute(
                "SELECT epic_id, display_name, premium, premium_order_id,"
                " autocoin FROM users WHERE epic_id = ?",
                (epic_id,)).fetchone()
        return dict(row) if row else None

    def set_premium(self, epic_id, source, order_id):
        with self._connect() as conn:
            conn.execute(
                "UPDATE users SET premium = ?, premium_order_id = ?,"
                " updated_utc = ? WHERE epic_id = ?",
                (source, order_id, utcnow(), epic_id))

    def add_coins(self, epic_id, delta):
        with self._connect() as conn:
            conn.execute(
                "UPDATE users SET autocoin = MAX(0, autocoin + ?),"
                " updated_utc = ? WHERE epic_id = ?",
                (delta, utcnow(), epic_id))

    def already_processed(self, ntype, order_id):
        with self._connect() as conn:
            row = conn.execute(
                "SELECT 1 FROM processed WHERE notification_type = ?"
                " AND order_id = ?", (ntype, order_id)).fetchone()
        return row is not None

    def mark_processed(self, ntype, order_id):
        with self._connect() as conn:
            conn.execute(
                "INSERT INTO processed (notification_type, order_id, received_utc)"
                " VALUES (?, ?, ?) ON CONFLICT(notification_type, order_id)"
                " DO NOTHING", (ntype, order_id, utcnow()))

    def record_grant(self, order_id, sku, quantity, coins, premium):
        with self._connect() as conn:
            conn.execute(
                "INSERT INTO grants (order_id, sku, quantity, coins, premium,"
                " granted_utc) VALUES (?, ?, ?, ?, ?, ?)",
                (order_id, sku, quantity, coins, premium or "", utcnow()))

    def grants_for_order(self, order_id):
        with self._connect() as conn:
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                "SELECT sku, quantity, coins, premium FROM grants"
                " WHERE order_id = ?", (order_id,)).fetchall()
        return [dict(r) for r in rows]


# ---------------------------------------------------------------- http

class Handler(BaseHTTPRequestHandler):
    server_version = "ChargeBackend/1.0"

    def log_message(self, fmt, *args):  # keep stdlib quiet; we log ourselves
        pass

    @property
    def db(self):
        return self.server.db

    @property
    def cfg(self):
        return self.server.cfg

    def _send(self, code, obj=None):
        body = b"" if obj is None else json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _v1_authorized(self):
        if not self.cfg.charge_api_key:
            return True
        return hmac.compare_digest(
            self.headers.get("X-Charge-Key", ""), self.cfg.charge_api_key)

    def _read_json(self):
        try:
            length = int(self.headers.get("Content-Length", 0) or 0)
        except ValueError:
            length = 0
        raw = self.rfile.read(length) if length > 0 else b""
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8"))

    # -- GET ------------------------------------------------------

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/healthz":
            return self._send(200, {"ok": True, "time": utcnow()})
        if path == "/v1/premium":
            return self._get_premium()
        return self._send(404, {"error": {"code": "NOT_FOUND",
                                          "message": "unknown endpoint"}})

    def _get_premium(self):
        if not self._v1_authorized():
            return self._send(401, {"error": {"code": "UNAUTHORIZED",
                                              "message": "bad or missing X-Charge-Key"}})
        qs = parse_qs(urlparse(self.path).query)
        epic_id = (qs.get("epic_account_id") or [""])[0]
        if not epic_id:
            return self._send(400, {"error": {"code": "MISSING_ID",
                                              "message": "epic_account_id is required"}})
        user = self.db.get_user(epic_id)
        if user is None:
            return self._send(200, {"epic_account_id": epic_id,
                                    "premium": False, "source": "none",
                                    "autocoin": 0})
        return self._send(200, {
            "epic_account_id": epic_id,
            "premium": user["premium"] not in ("", "none"),
            "source": user["premium"] or "none",
            "autocoin": user["autocoin"],
        })

    # -- POST -----------------------------------------------------

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/v1/register":
            return self._post_register()
        if path == "/webhooks/xsolla":
            return self._post_webhook()
        return self._send(404, {"error": {"code": "NOT_FOUND",
                                          "message": "unknown endpoint"}})

    def _post_register(self):
        if not self._v1_authorized():
            return self._send(401, {"error": {"code": "UNAUTHORIZED",
                                              "message": "bad or missing X-Charge-Key"}})
        try:
            payload = self._read_json()
        except ValueError:
            return self._send(400, {"error": {"code": "BAD_JSON",
                                              "message": "invalid JSON"}})
        epic_id = str(payload.get("epic_account_id", "") or "")
        if not epic_id:
            return self._send(400, {"error": {"code": "MISSING_ID",
                                              "message": "epic_account_id is required"}})
        name = str(payload.get("display_name", "") or "")
        self.db.ensure_user(epic_id, name or None)
        user = self.db.get_user(epic_id)
        log("register epic=%s name=%s" % (epic_id, name))
        return self._send(200, {
            "epic_account_id": epic_id,
            "premium": user["premium"] not in ("", "none"),
            "source": user["premium"] or "none",
            "autocoin": user["autocoin"],
        })

    def _post_webhook(self):
        try:
            length = int(self.headers.get("Content-Length", 0) or 0)
        except ValueError:
            length = 0
        raw = self.rfile.read(length) if length > 0 else b""
        if self.cfg.require_signature and not verify_signature(
                raw, self.headers.get("Authorization", ""), self.cfg.secret):
            log("webhook rejected: bad signature")
            return self._send(400, {"error": {"code": "INVALID_SIGNATURE",
                                              "message": "Invalid signature"}})
        try:
            payload = json.loads(raw.decode("utf-8")) if raw else {}
        except ValueError:
            return self._send(400, {"error": {"code": "BAD_JSON",
                                              "message": "invalid JSON"}})
        ntype = str(payload.get("notification_type", "") or "")
        order_id = extract_order_id(payload)
        user_id = extract_user_id(payload)

        if ntype == "user_validation":
            # v1: accept and remember the id. Strict mode (reject unknown
            # users) comes with game-side registration. Buying for an
            # arbitrary id only gifts that account at the buyer's expense.
            if user_id:
                self.db.ensure_user(user_id)
            log("user_validation user=%s -> accept" % user_id)
            return self._send(204)

        if ntype in ("order_paid", "order_canceled"):
            if not order_id:
                log("webhook %s without order id from user=%s: accept+ignore"
                    % (ntype, user_id))
                return self._send(204)
            if self.db.already_processed(ntype, order_id):
                log("webhook %s order=%s duplicate: accept+ignore"
                    % (ntype, order_id))
                return self._send(204)
            try:
                if ntype == "order_paid":
                    self._grant_order(order_id, user_id, payload)
                else:
                    self._revoke_order(order_id, user_id, payload)
            except Exception as exc:  # 500 -> Xsolla retries, as it should
                log("webhook %s order=%s FAILED: %s" % (ntype, order_id, exc))
                return self._send(500, {"error": {"code": "GRANT_FAILED",
                                                  "message": "retry later"}})
            self.db.mark_processed(ntype, order_id)
            return self._send(204)

        log("webhook unknown type=%s order=%s user=%s: accept+ignore"
            % (ntype, order_id, user_id))
        return self._send(204)

    # -- grant / revoke -------------------------------------------

    def _grant_order(self, order_id, user_id, payload):
        if not user_id:
            log("order_paid order=%s has no user id: accept+ignore" % order_id)
            return
        self.db.ensure_user(user_id)
        for sku, qty in extract_items(payload):
            if sku not in SKU_CATALOG:
                log("order_paid order=%s unknown sku=%s: skipped"
                    % (order_id, sku))
                continue
            premium_grant, coins_each = SKU_CATALOG[sku]
            if premium_grant:
                self.db.set_premium(user_id, premium_grant, order_id)
            coins = coins_each * qty
            if coins:
                self.db.add_coins(user_id, coins)
            self.db.record_grant(order_id, sku, qty, coins, premium_grant)
            log("order_paid order=%s user=%s sku=%s x%d -> +%d coins %s"
                % (order_id, user_id, sku, qty, coins,
                   ("premium=" + premium_grant) if premium_grant else ""))

    def _revoke_order(self, order_id, user_id, payload):
        rows = self.db.grants_for_order(order_id)
        if rows:
            if not user_id:
                log("order_canceled order=%s has grants but no user id: skip"
                    % order_id)
                return
            user = self.db.get_user(user_id)
            for row in rows:
                if row["premium"] and user and \
                        user["premium_order_id"] == order_id:
                    self.db.set_premium(user_id, "none", "")
                if row["coins"]:
                    self.db.add_coins(user_id, -row["coins"])
            log("order_canceled order=%s user=%s revoked %d grant(s)"
                % (order_id, user_id, len(rows)))
            return
        # No grant record (e.g. paid before grants existed): fall back to the
        # payload, but only touch premium tied to this order. Never 4xx/5xx.
        if not user_id:
            log("order_canceled order=%s no grants, no user: accept+ignore"
                % order_id)
            return
        user = self.db.get_user(user_id)
        for sku, qty in extract_items(payload):
            if sku not in SKU_CATALOG:
                continue
            premium_grant, coins_each = SKU_CATALOG[sku]
            if premium_grant and user and \
                    user["premium_order_id"] in ("", order_id):
                self.db.set_premium(user_id, "none", "")
            if coins_each * qty:
                self.db.add_coins(user_id, -coins_each * qty)
        log("order_canceled order=%s user=%s revoked from payload (no record)"
            % (order_id, user_id))


def run(cfg):
    if cfg.require_signature and not cfg.secret:
        log("WARNING: XSOLLA_SECRET_KEY is empty; all webhooks will fail"
            " signature checks until it is set.")
    if not cfg.require_signature:
        log("WARNING: signature verification DISABLED (local testing only).")
    db = ChargeDB(cfg.db_path)
    server = ThreadingHTTPServer(("0.0.0.0", cfg.port), Handler)
    server.cfg = cfg
    server.db = db
    log("listening on 0.0.0.0:%d db=%s" % (cfg.port, cfg.db_path))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    run(Config.from_env())
