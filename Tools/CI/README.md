# Optional GitHub Actions verification

`verify.yml` builds the native proof from a clean checkout, runs CTest/source-list checks, builds the browser experiment and uploads native evidence.

It is a **template, not an active workflow**. GitHub rejected activation because the current Arena GitHub connection lacks `workflows` permission. The source consolidation can still be pushed without that extra permission.

An authorized repository maintainer can copy this file to `.github/workflows/verify.yml` and commit it, or reconnect GitHub in Arena with the needed workflow permission before requesting activation. No password or token should be pasted into chat.
