# Xsolla Store + Premium + AutoCoin Economy Plan

Status: research report, no code. Written 2026-10-07 after the Photon link slice.
Project: Charge — Xsolla project ID `317410` (Free-to-Play PC account).
Owner holds the API key privately; it never enters the repo or chat.
Temporary key in use expires 2026-10-10; rotate to a fresh private key after.
Read this first, then answer the open decisions at the bottom.

## TL;DR answers

- **Xsolla as the store: yes, fits.** Catalog, checkout (Pay Station), virtual
  currencies, web-shop/buy-button, and a C++ SDK all exist.
- **Subscriptions: yes, Xsolla has them** (subscription management + recurring
  billing), alongside one-time purchases.
- **Crypto: players can PAY with crypto** (Crypto.com Pay in Pay Station), but
  Xsolla does not issue your own on-chain token. AutoCoin starts as a
  game-controlled virtual currency; a real blockchain token is a separate,
  much bigger project (see below).
- **Cost: 5% revenue share, no upfront cost.** Xsolla is Merchant of Record
  (tax, PCI, fraud). Real-world total is often 7–10% with channel costs.
- **Premium determination: Xsolla durable item or subscription → webhook to
  your backend → entitlement keyed by Epic account → `ResolvePremiumAccess()`.**

## 1. Setup: what to do in order

1. **Sign up to Publisher Account** (2FA on every login). As an individual,
   use your full name as the business name; pick your country. This also
   starts a draft licensing agreement — fill company/game data correctly now
   to speed up signing later.
2. **Create project**: type Game, name it (English), release platform PC,
   monetization = in-game store + subscriptions, engine custom/native.
   Note the **project ID** next to the project name — everything references it.
3. **Build the catalog** (Store → Items): create the SKUs below (premium pass,
   subscription plan, AutoCoin currency packs, cosmetic currencies). Prices in
   USD + ZAR at minimum; Xsolla handles regional pricing.
4. **Configure Pay Station**: theme, redirect/return URL, webhook URL
   (your backend endpoint; HTTPS). Enable the payment methods you want,
   including crypto (Crypto.com Pay) if desired.
5. **Test in sandbox**: pass `"sandbox": true` when creating the payment
   token, open `https://sandbox-secure.xsolla.com/paystation4/?token=…`,
   pay with test Visa `4111111111111111` exp `12/40` any CVV ZIP `12345`.
   No real money moves; sandbox works before you sign anything.
   Helper: `Tools/XsollaSandboxToken.ps1 -ApiKey …` prints and opens the
   sandbox URL for any SKU (default `entry_tokens`).
6. **Sign the licensing agreement** to unlock production payments, then flip
   the token mode to live.

Sources: project setup, sandbox/testing docs (links at the bottom).

## 2. Premium via the store (recommended design)

Keep Epic login exactly as-is. Xsolla never needs to be the identity provider:
every purchase carries **your** user ID (the Epic account ID), and Xsolla's
`user_validation` webhook lets your backend confirm the buyer is registered.

Catalog SKUs for premium:

| SKU | Type | Meaning |
|---|---|---|
| `premium_pass` | Durable virtual item, limit 1 per user, $19.99 | Owns premium forever (LIVE in catalog) |
| `premium_monthly` | Subscription, monthly recurring | Premium while subscribed (later) |
| `premium_yearly` | Subscription, yearly (discounted) | Premium while subscribed (later) |

Grant flow (all server-to-server; the game client never decides):

1. Player buys in the Xsolla UI (in-game browser overlay or web shop).
2. Xsolla fires **`order_paid`** to your backend with order ID, SKU, user ID.
3. Backend (idempotent on order ID) writes entitlement:
   `epic_account → { premium: lifetime | sub_expires_utc }`.
4. Game client asks your backend "is this Epic account premium?" at login;
   that answer feeds the existing **`ResolvePremiumAccess()`** seam, replacing
   the manual toggle. Subscription expiry/cancellation arrives via webhooks —
   backend flips the flag, next login (or periodic re-check) downgrades.
5. Refunds/chargebacks arrive as **`order_canceled`** — backend revokes.

Notes:

- Xsolla has a C++ store SDK with purchase validation helpers, so later the
  native client can open checkout and verify receipts directly.
- Until your backend exists, the manual toggle stays. The backend can start
  tiny: one HTTPS endpoint + one table.
- Xsolla Login (their auth product) is **not needed** — Epic remains the login.

## 3. Currency economy: Nitro, AutoCoin, cosmetics

Recommendation: three tiers. Everything server-authoritative; the client only
displays balances the server sends.

| Currency | Symbol | Source | Spend on |
|---|---|---|---|
| NITRO (soft, earned) | ⚡-like bolt | Race rewards, daily/weekly goals, events | Race entry fees, exchange → cosmetics |
| AUTOCOIN (hard, premium) | Hex-badge icon (see `Content/Icons/autocoin.png`) | Bought in AutoCoin packs via Xsolla; small drip from premium sub | Premium races entry, exchange → anything at better rates, exclusive cosmetics |
| MATTER (cosmetic) | — | Exchange from Nitro/AutoCoin, direct micro-packs | Material unlocks (visual only) |
| PAINT (cosmetic) | — | Exchange from Nitro/AutoCoin, direct micro-packs | Paints/liveries (visual only) |

Design rules:

- **Cosmetics never affect performance.** Matter/paint buy looks only. Say so
  in every store description — it kills pay-to-win complaints before they start.
- **Race entry is the main sink.** Every race costs Nitro (standard) or AutoCoin
  (premium/high-stakes); entry fees are deducted server-side when the lobby
  locks, refunded automatically if the race never starts.
- **AutoCoin packs** (Xsolla virtual-currency packages, separate items; unit
  price $0.01/coin as the anchor): Entry Tokens 100 @ $0.99
  (SKU `entry_tokens`), Podium Tokens 550 @ $4.99 (+10% bonus),
  Championship Tokens 1200 @ $9.99 (+20% bonus). Launch with Entry Tokens
  only; add Podium/Championship after the sandbox test passes. Bonus tiers are
  the standard conversion driver. Player always pays exactly the listed price;
  Xsolla's 5% (+ channel costs) comes out of our share, so $1 ≈ $0.90–0.93
  to us.
- **Premium subscribers** get a monthly AutoCoin drip (e.g. 300) — retention hook.

### Pay-per-match (arcade) pricing

Same concept as the race-entry sink: ranked/premium races are coin-operated,
like an arcade cabinet. Casual races stay free (or cost earned Nitro); only
ranked costs real-money currency.

- Ranked entry: **25 AutoCoin** (≈5 ZAR / $0.28 at 100 coins = $0.99).
- Session math: one race with lobby ≈ 5–10 min. Casuals play 3–5 races/day,
  regulars 5–10, grinders 15+.
- An engaged ranked player pays for ≈2–5 entries/day → R10–25/day.
- Example: 100 daily ranked players × R15 ≈ R1,500/day ≈ R45,000/month
  gross; Xsolla takes ~5% + channel costs (≈7–10% total).
- Winners are paid in Nitro (free currency) + cosmetics, never cash.
  No cash-out, ever. Entry deducted at lobby lock, refunded if the race
  never starts.

## 4. Exchange (Nitro → other currencies)

Run the exchange **in-game, server-side**, not in Xsolla. Xsolla sells AutoCoin
for real money; everything after that is your economy:

- Direction is one-way by default: Nitro/AutoCoin → Matter/Paint. No cash-out,
  no reverse exchange — this keeps you out of money-transmitter territory.
- Publish fixed rates with a small exchange fee (the fee is a hidden sink that
  fights inflation), e.g. 100 Nitro → 90 Matter after a 10% fee; AutoCoin
  converts at a flat 1 AutoCoin = 10 Matter / 10 Paint, no fee (premium perk).
- Rate changes are server config, never a client patch. Log every conversion
  (who, what, rate, timestamp) for support and balancing.
- Show a preview ("you get X") before confirm; confirmations are idempotent.

## 5. AutoCoin "crypto" reality check

Two very different things share the name "crypto":

1. **Crypto-flavored virtual currency (do this now).** AutoCoin lives in your
   database + Xsolla inventory, has a coin icon, exchange rates, the works.
   No blockchain, no gas fees, no wallet support tickets, no securities law.
   Players get the fantasy; you keep full control (anti-cheat, chargebacks,
   rebalancing).
2. **Real on-chain token (defer, maybe forever).** Needs a chain choice, audited
   contracts, wallet integration, liquidity, tax/legal opinions per country,
   and makes every economy rebalance a governance event. It also invites bots
   farming cash-outable currency. Nothing in phases 1–3 requires it, and
   nothing precludes adding it later as a AutoCoin withdrawal bridge.

Verdict: ship (1), revisit (2) only if the game demands withdrawals.

## 6. Anti-fraud rules (non-negotiable)

- Balances, entitlements, exchange, and entry fees are **server-side only**.
- Webhook handler is **idempotent on Xsolla order ID** (Xsolla retries).
- Verify webhook signatures; never trust SKU/price echoed from the client.
- `order_canceled` revokes premium and claws back undelivered currency.
- Rate-limit exchange + entry endpoints; log everything.

### Why this stops the classic cheats

- **Fake premium license:** no local license exists to forge. Premium is a
  server row keyed by Epic account, written only from signed Xsolla webhooks.
  The client's premium flag is display-only; flipping it unlocks nothing.
- **Premium Photon without paying:** Photon Custom Authentication calls our
  server on every connect; our server verifies Epic identity + the premium
  row before Photon lets them in. Interim: unguessable per-session room
  names issued only to premium players; host kicks unverified joiners.
- **Infinite money via memory editors:** balances are database rows. Every
  spend is an atomic server transaction (check + deduct); client numbers
  are display echoes. Cheat Engine can repaint the screen, not the database.
- **Fake race wins:** rewards are computed server-side from corroborated
  results (host + peers agree, times physically sane, earnings
  rate-limited). A lone client claim pays nothing.
- **Secrets:** the Xsolla API key + webhook secret live on the server only,
  never in the game build or repo.

## 7. Roadmap (phases)

- **Phase 0 — accounts (you, ~1 day):** Publisher Account, project, sandbox.
- **Phase 1 — catalog + sandbox:** premium SKUs, AutoCoin packs, test purchase
  end-to-end in sandbox, no game changes.
- **Phase 2 — backend stub:** webhook receiver + entitlement table + premium
  query endpoint (DONE in `Backend/`, 25 tests green); game wired
  (`QueryBackendPremium` in `Source/BackendClient.cpp`, localhost-ready,
  toggle fallback); still to do: host it with a public HTTPS URL.
- **Phase 3 — currencies in-game:** balances display, Nitro earn/spend, race
  entry fees, Matter/Paint exchange UI (reuse the lobby-panel patterns).
- **Phase 4 — production:** sign agreement, go live, monitor webhooks.
- **Phase 5 — later:** subscriptions polish, web shop promos, gift cards,
  on-chain bridge only if justified.

## 8. Open decisions (your call)

1. Premium model: lifetime pass, subscription, or both? Suggested prices?
2. AutoCoin pack sizes and bonus tiers?
3. Monthly AutoCoin drip amount for subscribers?
4. Exchange rates + fee?
5. Race entry fees (standard vs premium races)?
6. Real on-chain token ever, or virtual-only forever?

## Sources

- Publisher Account + project setup: [1](https://developers.xsolla.com/doc/in-game-store/integration-guide/create-project/), [2](https://developers.xsolla.com/sdk/unity/_archive/v1/integrate-complete-solution/set-up-publisher-project/)
- Virtual items, currencies, prices, limits: [3](https://developers.xsolla.com/items-catalog/items-type/virtual-items/)
- Webhooks (`order_paid`, `order_canceled`, `user_validation`) + granting: [4](https://developers.xsolla.com/solutions/web-shop/catalog-and-items/grant-purchases/), [5](https://developers.xsolla.com/sdk/mobile/windowstores/sdk/validation/)
- C++ purchase-validation SDK: [5](https://developers.xsolla.com/sdk/mobile/windowstores/sdk/validation/)
- Subscriptions + recurring billing: [6](https://noda.live/articles/xsolla-vs-stripe), [7](https://xsolla.com/newsroom/empowering-developers-to-monetize-anywhere-xsolla-expands-platform-support-for-cross-platform-direct-to-consumer-commerce)
- Crypto payments via Crypto.com Pay: [8](https://crypto.com/en/company-news/xsolla-and-crypto-com-partner-to-integrate-payment-solutions)
- 5% revenue share, Merchant of Record: [9](https://toolradar.com/tools/xsolla)
- Sandbox mode + test cards: [10](https://developers.xsolla.com/doc/pay-station/testing/general-info/), [11](https://developers.xsolla.com/dev-resources/testing/sandbox-mode/test-cards-in-sandbox/)
