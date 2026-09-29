# Frontier shop — manual orders, many payment methods

Buyers pick a payment method → pay → click "I've paid". You check your PayPal / Payoneer / bank app, click **Mark paid** in `/admin`, and the buyer's private order page unlocks the download.
No payment company integration, no database, no npm packages. Just Node.js.

## 1. Set your payment details
Edit `payment-methods.json`:
- `product`: name, price, currency
- For each method: `"enabled": true/false`, then replace every `YOUR-...` placeholder.
- `{ref}`, `{amount}` and `{currency}` are filled in automatically.

Methods included: PayPal, Payoneer payment request, Wise, SWIFT to Capitec, SA EFT, card link (PayFast/Yoco/Paystack, off), crypto (off).

## 2. Run it (Windows, Mac or Linux)
Install Node.js 18+ from https://nodejs.org, then in this folder:

```bash
# Windows PowerShell
$env:ADMIN_PASSWORD="choose-a-long-password"; $env:DOWNLOAD_FILE="Frontier.zip"; node server.js

# Mac / Linux
ADMIN_PASSWORD="choose-a-long-password" DOWNLOAD_FILE="Frontier.zip" node server.js
```

Open http://localhost:3000. Your admin page is http://localhost:3000/admin (any username + your password).

| Setting | What it does |
|---|---|
| `ADMIN_PASSWORD` | **Required** for /admin |
| `DOWNLOAD_FILE` | Game file name, put inside `server/files/` |
| `DOWNLOAD_URL` | …or an external link instead (Google Drive, GitHub Releases) |
| `MAX_DOWNLOADS` | Downloads allowed per order (default 5) |
| `CONTACT_EMAIL` | Shown to buyers |
| `PORT` | Default 3000 |

Orders are saved in `server/data/orders.json`. **Back this file up.** It's excluded from Git so buyer details never get pushed.

## 3. Put it online for free, on your own domain
Run it on your own PC and expose it with **Cloudflare Tunnel** (free, no port forwarding, HTTPS included):
1. Install `cloudflared`: https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/downloads/
2. `cloudflared tunnel login` (pick your domain)
3. `cloudflared tunnel create frontier`
4. `cloudflared tunnel route dns frontier shop.YOURDOMAIN`
5. `cloudflared tunnel run --url http://localhost:3000 frontier`

Your shop is then live at `https://shop.YOURDOMAIN`. It's only online while your PC is on, but buyers can still pay anytime. They just get the download when you confirm.

## Daily routine
1. Open `/admin` → **Payment being checked**.
2. Find the payment in your own PayPal / Payoneer / bank app using the `FRN-XXXXXX` reference.
3. Click **✔ Mark paid**, then email the buyer the link from "buyer page".

⚠️ Only mark paid when the money is in your account. Never trust screenshots, and never refund "overpayments".
