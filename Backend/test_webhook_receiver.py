#!/usr/bin/env python3
"""Tests for webhook_receiver.py (stdlib unittest). Run:
  python3 -m unittest discover -s Backend -p 'test_*.py' -v
"""

import hashlib
import json
import os
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import webhook_receiver as wr

SECRET = "test-secret-key"


def sign(raw: bytes) -> str:
    return "Signature " + hashlib.sha1(raw + SECRET.encode()).hexdigest()


class LiveServerCase(unittest.TestCase):
    require_signature = True
    charge_api_key = ""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        cfg = wr.Config(secret=SECRET,
                        require_signature=self.require_signature,
                        charge_api_key=self.charge_api_key,
                        port=0,
                        db_path=os.path.join(self.tmp.name, "t.db"))
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), wr.Handler)
        self.server.cfg = cfg
        self.server.db = wr.ChargeDB(cfg.db_path)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.tmp.cleanup()

    # -- helpers ---------------------------------------------------

    def _request(self, method, path, obj=None, headers=None):
        data = None
        if obj is not None:
            data = json.dumps(obj).encode("utf-8")
        req = urllib.request.Request(
            "http://127.0.0.1:%d%s" % (self.port, path),
            data=data, method=method,
            headers={"Content-Type": "application/json",
                     **(headers or {})})
        try:
            with urllib.request.urlopen(req) as resp:
                raw = resp.read()
                return resp.status, json.loads(raw) if raw else None
        except urllib.error.HTTPError as exc:
            raw = exc.read()
            try:
                return exc.code, json.loads(raw) if raw else None
            except ValueError:
                return exc.code, None

    def _webhook(self, payload, secret=SECRET, no_auth=False):
        raw = json.dumps(payload).encode("utf-8")
        headers = {}
        if not no_auth:
            headers["Authorization"] = "Signature " + hashlib.sha1(
                raw + secret.encode()).hexdigest()
        req = urllib.request.Request(
            "http://127.0.0.1:%d/webhooks/xsolla" % self.port,
            data=raw, method="POST",
            headers={"Content-Type": "application/json", **headers})
        try:
            with urllib.request.urlopen(req) as resp:
                return resp.status
        except urllib.error.HTTPError as exc:
            exc.read()
            return exc.code

    def _premium(self, epic_id):
        code, body = self._request(
            "GET", "/v1/premium?epic_account_id=" + urllib.parse.quote(epic_id))
        return code, body

    # -- tests ------------------------------------------------------

    def test_healthz(self):
        code, body = self._request("GET", "/healthz")
        self.assertEqual(code, 200)
        self.assertTrue(body["ok"])

    def test_webhook_rejects_bad_signature(self):
        code = self._webhook({"notification_type": "order_paid"},
                             secret="wrong")
        self.assertEqual(code, 400)

    def test_webhook_rejects_missing_signature(self):
        code = self._webhook({"notification_type": "order_paid"}, no_auth=True)
        self.assertEqual(code, 400)

    def test_user_validation_accepts_and_remembers(self):
        code = self._webhook({"notification_type": "user_validation",
                              "user": {"id": "epic-alice"}})
        self.assertEqual(code, 204)
        code, body = self._premium("epic-alice")
        self.assertEqual(code, 200)
        self.assertFalse(body["premium"])
        self.assertEqual(body["autocoin"], 0)

    def test_order_paid_grants_coins_idempotently(self):
        payload = {"notification_type": "order_paid",
                   "order": {"id": 9001},
                   "user": {"id": {"value": "epic-bob"}},
                   "items": [{"sku": "entry_tokens", "quantity": 1}]}
        self.assertEqual(self._webhook(payload), 204)
        _, body = self._premium("epic-bob")
        self.assertEqual(body["autocoin"], 100)
        self.assertEqual(self._webhook(payload), 204)  # Xsolla retry
        _, body = self._premium("epic-bob")
        self.assertEqual(body["autocoin"], 100)  # not doubled

    def test_order_paid_grants_premium(self):
        payload = {"notification_type": "order_paid",
                   "order": {"id": 9002},
                   "user": {"id": "epic-cara"},
                   "purchase": {"items": [{"sku": "premium_pass"}]}}
        self.assertEqual(self._webhook(payload), 204)
        _, body = self._premium("epic-cara")
        self.assertTrue(body["premium"])
        self.assertEqual(body["source"], "lifetime")

    def test_order_paid_unknown_sku_still_204(self):
        payload = {"notification_type": "order_paid",
                   "order": {"id": 9003},
                   "user": {"id": "epic-dan"},
                   "items": [{"sku": "mystery_box", "quantity": 1}]}
        self.assertEqual(self._webhook(payload), 204)
        _, body = self._premium("epic-dan")
        self.assertFalse(body["premium"])
        self.assertEqual(body["autocoin"], 0)

    def test_order_canceled_claws_back_coins(self):
        paid = {"notification_type": "order_paid", "order": {"id": 9004},
                "user": {"id": "epic-erin"},
                "items": [{"sku": "entry_tokens", "quantity": 2}]}
        canceled = dict(paid, notification_type="order_canceled")
        self.assertEqual(self._webhook(paid), 204)
        _, body = self._premium("epic-erin")
        self.assertEqual(body["autocoin"], 200)
        self.assertEqual(self._webhook(canceled), 204)
        _, body = self._premium("epic-erin")
        self.assertEqual(body["autocoin"], 0)

    def test_order_canceled_revokes_only_matching_premium(self):
        paid_old = {"notification_type": "order_paid", "order": {"id": 9005},
                    "user": {"id": "epic-finn"},
                    "items": [{"sku": "premium_pass", "quantity": 1}]}
        paid_new = dict(paid_old, order={"id": 9006})
        cancel_old = dict(paid_old, notification_type="order_canceled")
        self.assertEqual(self._webhook(paid_old), 204)
        self.assertEqual(self._webhook(paid_new), 204)
        self.assertEqual(self._webhook(cancel_old), 204)
        _, body = self._premium("epic-finn")
        self.assertTrue(body["premium"])  # newer order's premium survives

    def test_unknown_notification_type_accepted(self):
        code = self._webhook({"notification_type": "something_new",
                              "order": {"id": 9007}})
        self.assertEqual(code, 204)

    def test_register_and_premium_roundtrip(self):
        code, body = self._request("POST", "/v1/register",
                                   {"epic_account_id": "epic-gus",
                                    "display_name": "Gus"})
        self.assertEqual(code, 200)
        self.assertFalse(body["premium"])
        code, _ = self._request("POST", "/v1/register", {})
        self.assertEqual(code, 400)
        code, _ = self._request("GET", "/v1/premium")
        self.assertEqual(code, 400)

    def test_user_id_shapes(self):
        self.assertEqual(wr.extract_user_id({"user": {"id": "a"}}), "a")
        self.assertEqual(wr.extract_user_id({"user": {"id": {"value": "b"}}}),
                         "b")
        self.assertEqual(wr.extract_user_id({"user": {"external_id": "c"}}),
                         "c")
        self.assertEqual(wr.extract_user_id(
            {"custom_parameters": {"epic_account_id": "d"}}), "d")
        self.assertEqual(wr.extract_user_id({}), "")


class ApiKeyCase(LiveServerCase):
    charge_api_key = "game-secret"

    def _request(self, method, path, obj=None, headers=None, no_key=False):
        headers = dict(headers or {})
        if not no_key:
            headers.setdefault("X-Charge-Key", "game-secret")
        return super()._request(method, path, obj, headers)

    def test_v1_requires_key(self):
        code, _ = self._request("GET", "/v1/premium?epic_account_id=x",
                                no_key=True)
        self.assertEqual(code, 401)
        code, body = self._request("GET", "/v1/premium?epic_account_id=x",
                                   headers={"X-Charge-Key": "game-secret"})
        self.assertEqual(code, 200)
        self.assertFalse(body["premium"])

    # webhook + healthz are unaffected by the game key
    def test_healthz(self):
        code, _ = self._request("GET", "/healthz")
        self.assertEqual(code, 200)


if __name__ == "__main__":
    unittest.main(verbosity=2)
