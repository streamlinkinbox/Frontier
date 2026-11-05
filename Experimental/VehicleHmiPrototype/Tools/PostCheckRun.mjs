//============================================================================================================================================
//                                                           POSTCHECKRUN.MJS
//============================================================================================================================================
// 🧩 Publishes a job's key output (test counts, the mode table, the benchmark table) as a GitHub CHECK RUN on the commit, so it can be read back through the REST API:
//
//        gh api repos/OWNER/REPO/commits/SHA/check-runs --jq '.check_runs[] | [.name, .conclusion, .output.title] | @tsv'
//        gh api repos/OWNER/REPO/check-runs/ID --jq .output.text
//
//    Why: job logs and artifacts are served from blob-storage hosts that a locked-down client (the sandbox this was developed in) cannot reach, while api.github.com can be. A check
//    run's output is up to 65 535 characters of markdown, which is plenty for a table. It needs `permissions: checks: write` on the job.
//
//    node PostCheckRun.mjs --name "Report: unit tests" --file log.txt [--title "87 passed"] [--summary "text"] [--conclusion success|failure|neutral] [--filter tap] [--raw] [--sha SHA]
//    --filter tap  keeps node --test's counts, test names, failures and printed diagnostics, and titles the check run with the counts;  --raw  does not wrap the text in a code block (markdown tables)
//    env: GITHUB_TOKEN, GITHUB_REPOSITORY, GITHUB_SHA     (--dry prints the request instead of sending it)

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const MaximumText = 60000;     // GitHub allows 65 535; leave room for the wrapper

// ➡️ For TAP output (node --test): the summary counts, every failing test with its message, and the names of the passing and skipped ones, without the thousands of lines of YAML
//    between them. Anything else passes through unchanged.
export function FilterTap(Text)
{
    const Lines = Text.split("\n"), Kept = [];
    let Failing = false;
    for (const Line of Lines)
    {
        if (/^\s*(not ok|ok) \d+ - /.test(Line)) { Failing = /^\s*not ok/.test(Line); Kept.push(Line.replace(/\s+$/, "")); continue; }
        if (/^# (tests|suites|pass|fail|cancelled|skipped|todo|duration_ms)\b/.test(Line)) { Kept.push(Line); continue; }
        if (/^\s*# (?!Subtest)/.test(Line)) { Kept.push(Line.replace(/\s+$/, "")); continue; }            // diagnostics a test chose to print (measured values)
        if (Failing && /^\s+(error|message|expected|actual|code|location):?/.test(Line)) Kept.push(Line.replace(/\s+$/, ""));
    }
    return Kept.join("\n");
}

// ➡️ The text cut to fit, keeping the END (where failures and summaries are) and saying so.
export function Fit(Text, Limit = MaximumText)
{
    if (Text.length <= Limit) return Text;
    return `… (${Text.length - Limit} characters cut from the start)\n` + Text.slice(Text.length - Limit);
}

// ➡️ "87 passed, 0 failed, 9 skipped" from node --test's counts.
export function TapTitle(Text)
{
    const Count = (Key) => { const Match = Text.match(new RegExp(`^# ${Key} (\\d+)`, "m")); return Match ? Number(Match[1]) : null; };
    const [Pass, Fail, Skipped] = [Count("pass"), Count("fail"), Count("skipped")];
    return Pass === null ? "" : `${Pass} passed, ${Fail ?? 0} failed, ${Skipped ?? 0} skipped`;
}

export function BuildRequest({ Name, Title, Summary, Text, Conclusion, Sha, Repository, Raw = false })
{
    if (!["success", "failure", "neutral", "cancelled", "skipped", "timed_out", "action_required"].includes(Conclusion)) throw new Error(`conclusion "${Conclusion}" is not a check-run conclusion`);
    if (!Name || !Sha || !Repository) throw new Error("name, sha and repository are required");
    return { Url: `https://api.github.com/repos/${Repository}/check-runs`,
             Body: { name: Name, head_sha: Sha, status: "completed", conclusion: Conclusion,
                     output: { title: (Title || Name).slice(0, 250), summary: (Summary || Title || Name).slice(0, 60000), text: Raw ? Fit(Text || "") : "```text\n" + Fit(Text || "").replace(/```/g, "'''") + "\n```" } } };
}

// ➡️ A GitHub workflow command: `::notice title=…::message`, which becomes an ANNOTATION on the job's check run. Annotations need no permission to create and can be read back with
//    gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations --jq '.[] | [.title, .message] | @tsv'        (JOB_ID: gh run view RUN --json jobs)
//    Escaping follows the runner's rules: data escapes % \r \n, a property additionally escapes : and ,.
export const EscapeData = (Text) => String(Text).replace(/%/g, "%25").replace(/\r/g, "%0D").replace(/\n/g, "%0A");
export const EscapeProperty = (Text) => EscapeData(Text).replace(/:/g, "%3A").replace(/,/g, "%2C");
export const AnnotationLimit = 30000;       // the API accepts 64 KB per annotation message
export function Annotation(Title, Text, Level = "notice")
{
    return `::${Level} title=${EscapeProperty(Title)}::${EscapeData(Fit(Text, AnnotationLimit))}`;
}

export function ParseArguments(Argv)
{
    const Options = { name: "", title: "", summary: "", file: "", conclusion: "neutral", filter: "", raw: false, sha: process.env.GITHUB_SHA || "", repository: process.env.GITHUB_REPOSITORY || "", dry: false };
    for (let Index = 0; Index < Argv.length; ++Index)
    {
        const Key = Argv[Index].replace(/^--/, "");
        if (!(Key in Options)) throw new Error(`unknown option ${Argv[Index]}`);
        if (Key === "dry" || Key === "raw") { Options[Key] = true; continue; }
        Options[Key] = Argv[++Index];
    }
    return Options;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
{
    const Options = ParseArguments(process.argv.slice(2));
    let Text = Options.file && fs.existsSync(Options.file) ? fs.readFileSync(Options.file, "utf8") : "(no output file)";
    const Full = Text;
    if (Options.filter === "tap") Text = FilterTap(Text);
    const Request = BuildRequest({ Raw: Options.raw, Name: Options.name, Title: Options.title || (Options.filter === "tap" ? TapTitle(Full) : ""), Summary: Options.summary, Text, Conclusion: Options.conclusion, Sha: Options.sha, Repository: Options.repository });
    if (Options.dry) { console.log(JSON.stringify(Request, null, 1)); process.exit(0); }
    // the permission-free channel first: an annotation on the job (title, then the same text the check run carries)
    console.log(Annotation(`${Options.name}${Request.Body.output.title && Request.Body.output.title !== Options.name ? ": " + Request.Body.output.title : ""}`, Text));
    // 📝 Reporting must never turn a green job red: any failure to publish (no network, a missing permission) is printed and the exit status stays 0.
    try
    {
        const Response = await fetch(Request.Url, { method: "POST", headers: { Authorization: `Bearer ${process.env.GITHUB_TOKEN}`, Accept: "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28", "Content-Type": "application/json" }, body: JSON.stringify(Request.Body) });
        const Reply = await Response.text();
        if (!Response.ok) console.error(`check run not posted: HTTP ${Response.status} ${Reply.slice(0, 400)}`);
        else console.log(`posted check run "${Options.name}" (${Options.conclusion}) at ${JSON.parse(Reply).html_url}`);
    }
    catch (Failure)
    {
        console.error(`check run not posted: ${Failure.message}${Failure.cause ? " (" + (Failure.cause.message || Failure.cause) + ")" : ""}`);
    }
}
