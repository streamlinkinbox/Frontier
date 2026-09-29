# Free website + domain verified for Epic Online Services ($0, 2026)

Epic checks domain ownership with a **DNS TXT record**. So `*.github.io`, `*.pages.dev`, `*.vercel.app` etc. will NOT work — you need a domain whose DNS you control.

**Stack:** DigitalPlat FreeDomain (free domain) → Cloudflare (free DNS) → Cloudflare Pages (free hosting). No credit card needed.

## 1. Get a free domain (about 5 min)
1. Go to https://dash.domain.digitalplat.org/ and sign up.
2. Search for a name, e.g. `frontiergame`, and pick an ending: `.dpdns.org`, `.us.kg`, `.qzz.io`, `.xx.kg`, `.qd.je`.
3. Register it (free, max 3 per account). Leave the nameserver page open.

## 2. Put it on Cloudflare DNS
1. Sign up at https://dash.cloudflare.com → **Add a domain** → type your full domain (e.g. `frontiergame.dpdns.org`) → Free plan.
2. Cloudflare shows 2 nameservers (like `xxx.ns.cloudflare.com`).
3. Back in the DigitalPlat dashboard, set those 2 nameservers on your domain.
4. Wait until Cloudflare says **Active** (minutes to a few hours).

## 3. Host this site
1. In Cloudflare: **Workers & Pages → Create → Pages → Connect to Git** → choose this repo.
2. Build command: *(empty)*. Output directory: `site`.
3. After deploy: **Custom domains → Set up a custom domain** → your domain. Cloudflare adds the DNS record for you.
4. Replace `YOURDOMAIN` in `site/*.html` with your real domain/email.

## 4. Verify in Epic Developer Portal
1. https://dev.epicgames.com/portal → your organization → **Organization → Settings → Domains → Add Domain**.
2. Enter your domain exactly (e.g. `frontiergame.dpdns.org`). Epic gives you a **TXT Domain Secret**.
3. Cloudflare → your domain → **DNS → Records → Add record**:
   - Type: `TXT`
   - Name: `@`
   - Content: *paste the secret*
4. Save, wait a few minutes, then click **Check txt record** in the Epic portal.
5. An Epic moderator then reviews it manually. The Privacy Policy and Terms pages here help with the brand review.

Check the record is live: `nslookup -type=TXT frontiergame.dpdns.org`

## Tips
- Put the TXT record on the **exact** domain you typed into Epic.
- Log in to DigitalPlat once in a while and renew when asked, so you don't lose the domain.
- If Epic's moderator rejects a free domain ending, a cheap `.xyz`/`.com` (~$1–10) is the fallback — the rest of these steps stay the same.
