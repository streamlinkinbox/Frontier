#====================================================================================================================================
#                                                       INSPECTIONHOST.PY
#====================================================================================================================================
# 🧩 Serves the GeometryProcessing viewer, brokers specimen requests to the C++ inspection process, rebuilds on source change

"""
Run:  python Exhibits/GeometryProcessing/Code/Tooling/InspectionHost.py [--port 8740] [--build-dir DIR] [--cmake CMAKE]

Endpoints
    GET /                         Viewer/index.html
    GET /api/catalogue            specimen names, descriptions and default parameters
    GET /api/specimen?name=N&K=V  construct + audit + encode one specimen (one line to the persistent C++ process)
    GET /api/slice?name=N&K=V     generalized-winding-number slice of a model or Boolean result (Axis, Offset, Resolution)
    GET /api/tessellation?name=N&K=V  shared-edge CDT PolygonComplex of a model, Boolean or blend result (SagRatio) with N1–N7 gates
    GET /api/blend?name=N&K=V     rolling-ball blend of a catalogue specimen (Size, Source=1 shows the body before), provenance + ΔV check
    GET /api/faceop?name=N&K=V    face operation of a catalogue specimen (Amount scales the last step, Source=1 shows the body before), provenance + V check
    GET /api/specimen?name=SketchName&Label=V   solve a catalogue sketch with edited dimensions (DOF, rank, conflicts, profile)
    GET /api/specimen?name=TreeName&P=V&Upto=K  regenerate a feature tree (parameters, replay up to step K): names, resolutions, V check
    GET /api/validation?name=N    run TopologyValidation | ... | FaceOperationValidation | SketchValidation | FeatureValidation | CorpusValidation, returns markdown
    GET /api/status               build state (Building | Ready | Failed), stale flag, build log, revision
    GET /api/events               Server-Sent Events: build state changes
"""

import argparse
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

#------------------------------------------------------------------------------------------------------------------------
#                                                    CONFIGURATION
#------------------------------------------------------------------------------------------------------------------------

ProjectRoot     = Path(__file__).resolve().parent.parent                                    # Exhibits/GeometryProcessing/Code
ViewerRoot      = ProjectRoot / "Viewer"
WatchedRoots    = [ProjectRoot / "Source", ProjectRoot / "Validation", ProjectRoot / "CMakeLists.txt"]
WatchInterval   = 0.5                                                                        # [s]  - Source scan period
RequestTimeout  = 30.0                                                                       # [s]  - One specimen request
ValidationNames = { "TopologyValidation", "PredicateValidation", "IntersectionValidation", "ContainmentValidation", "BooleanValidation", "TessellationValidation", "BlendValidation", "FaceOperationValidation", "SketchValidation", "FeatureValidation", "CorpusValidation" }
ContentKinds    = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".json": "application/json" }

#------------------------------------------------------------------------------------------------------------------------
#                                                    BUILD DIVISION
#------------------------------------------------------------------------------------------------------------------------

class BuildDivision:
    """Configures and builds with CMake; reports state to every SSE subscriber."""

    def __init__(self, BuildDirectory: Path, CMakeProgram: str, Generator: str | None, MakeProgram: str | None):
        self.BuildDirectory = BuildDirectory
        self.CMakeProgram   = CMakeProgram
        self.Generator      = Generator
        self.MakeProgram    = MakeProgram
        self.State          = "Building"                                                     # Building | Ready | Failed
        self.StaleCondition = False                                                          # Running binary predates the sources
        self.Log            = ""
        self.Revision       = 0                                                              # [-]  - Successful builds so far
        self.Subscribers    = []                                                             # SSE queues
        self.Lock           = threading.Lock()

    def Snapshot(self) -> dict:
        return { "State": self.State, "Stale": self.StaleCondition, "Log": self.Log[-12000:], "Revision": self.Revision }

    def Broadcast(self):
        Message = json.dumps(self.Snapshot())
        with self.Lock:
            for Subscriber in list(self.Subscribers):
                Subscriber.put(Message)

    def LocateExecutable(self, Name: str) -> Path | None:
        Suffix = ".exe" if os.name == "nt" else ""
        for Candidate in [self.BuildDirectory / (Name + Suffix)] + [self.BuildDirectory / Configuration / (Name + Suffix) for Configuration in ("RelWithDebInfo", "Release", "Debug")]:
            if Candidate.exists():
                return Candidate
        return None

    def Build(self) -> bool:
        self.State = "Building"
        self.Broadcast()
        Commands = []
        if not (self.BuildDirectory / "CMakeCache.txt").exists():
            Configure = [self.CMakeProgram, "-S", str(ProjectRoot), "-B", str(self.BuildDirectory)]
            if self.Generator:   Configure += ["-G", self.Generator]
            if self.MakeProgram: Configure += [f"-DCMAKE_MAKE_PROGRAM={self.MakeProgram}"]
            Commands.append(Configure)
        Commands.append([self.CMakeProgram, "--build", str(self.BuildDirectory), "--config", "RelWithDebInfo"])
        Output = []
        Started = time.perf_counter()
        for Command in Commands:
            Completed = subprocess.run(Command, capture_output=True, text=True, errors="replace")
            Output.append("$ " + " ".join(Command) + "\n" + Completed.stdout + Completed.stderr)
            if Completed.returncode != 0:
                self.State, self.Log = "Failed", "\n".join(Output)
                self.StaleCondition = True                                                   # Old binary (if any) keeps serving
                self.Broadcast()
                return False
        self.State, self.StaleCondition = "Ready", False
        self.Log = "\n".join(Output) + f"\nBuilt in {time.perf_counter() - Started:.1f} s"
        self.Revision += 1
        self.Broadcast()
        return True

#------------------------------------------------------------------------------------------------------------------------
#                                                    INSPECTION PROCESS
#------------------------------------------------------------------------------------------------------------------------

class InspectionProcess:
    """One persistent GeometryInspection process; one request line in, one JSON line out."""

    def __init__(self, Builds: BuildDivision):
        self.Builds  = Builds
        self.Process = None
        self.Lock    = threading.Lock()

    def Restart(self):
        with self.Lock:
            self.Terminate()
            Executable = self.Builds.LocateExecutable("GeometryInspection")
            if Executable is None:
                return
            self.Process = subprocess.Popen([str(Executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)

    def Terminate(self):
        if self.Process and self.Process.poll() is None:
            try:
                self.Process.stdin.write("quit\n")
                self.Process.stdin.flush()
                self.Process.wait(timeout=2)
            except Exception:
                self.Process.kill()
        self.Process = None

    def Request(self, Line: str) -> str:
        with self.Lock:
            if self.Process is None or self.Process.poll() is not None:
                Executable = self.Builds.LocateExecutable("GeometryInspection")
                if Executable is None:
                    return json.dumps({ "Refusal": "GeometryInspection is not built yet" })
                self.Process = subprocess.Popen([str(Executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
            try:
                self.Process.stdin.write(Line + "\n")
                self.Process.stdin.flush()
                Answer = self.Process.stdout.readline()
            except (BrokenPipeError, OSError) as Failure:
                self.Process = None
                return json.dumps({ "Refusal": f"inspection process failed: {Failure}" })
            if not Answer:
                Detail = self.Process.stderr.read() if self.Process.poll() is not None else ""
                self.Process = None
                return json.dumps({ "Refusal": "inspection process exited (crash?) " + Detail[-2000:] })
            return Answer

#------------------------------------------------------------------------------------------------------------------------
#                                                    SOURCE WATCH
#------------------------------------------------------------------------------------------------------------------------

def ScanSources() -> dict:
    Stamps = {}
    for Root in WatchedRoots:
        Paths = [Root] if Root.is_file() else Root.rglob("*") if Root.exists() else []
        for SourcePath in Paths:
            if SourcePath.is_file() and SourcePath.suffix in (".h", ".cpp", ".txt"):
                Stamps[str(SourcePath)] = SourcePath.stat().st_mtime
    return Stamps

def WatchSources(Builds: BuildDivision, Inspection: InspectionProcess):
    Previous = ScanSources()
    while True:
        time.sleep(WatchInterval)
        Current = ScanSources()
        if Current != Previous:
            time.sleep(WatchInterval)                                                        # Let editors finish writing
            Previous = ScanSources()
            Builds.StaleCondition = True
            if Builds.Build():
                Inspection.Restart()
                Builds.Broadcast()

#------------------------------------------------------------------------------------------------------------------------
#                                                    REQUEST ROUTING
#------------------------------------------------------------------------------------------------------------------------

def ConstructHandlerClass(Builds: BuildDivision, Inspection: InspectionProcess):
    class InspectionRequests(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, Format, *Arguments):
            pass

        def Respond(self, Status: int, Body: bytes, Kind: str):
            self.send_response(Status)
            self.send_header("Content-Type", Kind)
            self.send_header("Content-Length", str(len(Body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(Body)

        def RespondJson(self, Text: str):
            self.Respond(200, Text.encode("utf-8"), "application/json")

        def do_GET(self):
            Address = urlparse(self.path)
            Query   = { Key: Values[-1] for Key, Values in parse_qs(Address.query).items() }
            if Address.path == "/api/catalogue":
                return self.RespondJson(Inspection.Request("catalogue"))
            if Address.path in ("/api/specimen", "/api/slice", "/api/tessellation", "/api/blend", "/api/faceop"):
                # 📝 Blend specimens answer the specimen verb (the C++ host dispatches on the catalogue name).
                Verb = {"/api/specimen": "specimen", "/api/slice": "slice", "/api/tessellation": "tessellate", "/api/blend": "specimen", "/api/faceop": "specimen"}[Address.path]
                Name = Query.pop("name", "")
                if not Name.isidentifier():
                    return self.RespondJson(json.dumps({ "Refusal": "specimen name must be an identifier" }))
                Pairs = []
                for Key, Value in Query.items():
                    try:
                        Pairs.append(f"{Key}={float(Value)!r}")
                    except ValueError:
                        return self.RespondJson(json.dumps({ "Refusal": f"parameter {Key} is not a number" }))
                return self.RespondJson(Inspection.Request(" ".join([Verb, Name] + Pairs)))
            if Address.path == "/api/validation":
                Name = Query.get("name", "")
                Executable = Builds.LocateExecutable(Name) if Name in ValidationNames else None
                if Executable is None:
                    return self.RespondJson(json.dumps({ "Refusal": f"no validation named {Name!r} is built" }))
                Started   = time.perf_counter()
                Completed = subprocess.run([str(Executable)], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
                return self.RespondJson(json.dumps({ "Name": Name, "ExitCode": Completed.returncode, "Markdown": Completed.stdout + (Completed.stderr if Completed.returncode != 0 else ""), "Seconds": time.perf_counter() - Started }))
            if Address.path == "/api/status":
                return self.RespondJson(json.dumps(Builds.Snapshot()))
            if Address.path == "/api/events":
                return self.StreamEvents()
            Relative = "index.html" if Address.path in ("", "/") else Address.path.lstrip("/")
            Target   = (ViewerRoot / Relative).resolve()
            if ViewerRoot.resolve() not in Target.parents or not Target.is_file():
                return self.Respond(404, b"not found", "text/plain")
            self.Respond(200, Target.read_bytes(), ContentKinds.get(Target.suffix, "application/octet-stream"))

        def StreamEvents(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Accel-Buffering", "no")
            self.end_headers()
            Subscriber = queue.Queue()
            with Builds.Lock:
                Builds.Subscribers.append(Subscriber)
            try:
                Subscriber.put(json.dumps(Builds.Snapshot()))
                while True:
                    try:
                        Message = Subscriber.get(timeout=15)
                        self.wfile.write(f"data: {Message}\n\n".encode("utf-8"))
                    except queue.Empty:
                        self.wfile.write(b": heartbeat\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                with Builds.Lock:
                    Builds.Subscribers.remove(Subscriber)

    return InspectionRequests

#------------------------------------------------------------------------------------------------------------------------
#                                                    MAIN
#------------------------------------------------------------------------------------------------------------------------

def main():
    Parser = argparse.ArgumentParser(description="GeometryProcessing inspection host")
    Parser.add_argument("--port", type=int, default=8740)
    Parser.add_argument("--bind", default="0.0.0.0")
    Parser.add_argument("--build-dir", default=str(ProjectRoot / "build"))
    Parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake")
    Parser.add_argument("--generator", default=None, help="e.g. Ninja (default: CMake's platform default)")
    Parser.add_argument("--make-program", default=None, help="path to ninja when it is not on PATH")
    Arguments = Parser.parse_args()

    Builds     = BuildDivision(Path(Arguments.build_dir).resolve(), Arguments.cmake, Arguments.generator, Arguments.make_program)
    Inspection = InspectionProcess(Builds)
    Builds.Build()
    Inspection.Restart()
    threading.Thread(target=WatchSources, args=(Builds, Inspection), daemon=True).start()

    Server = ThreadingHTTPServer((Arguments.bind, Arguments.port), ConstructHandlerClass(Builds, Inspection))
    Server.daemon_threads = True
    print(f"GeometryProcessing inspection host: http://{Arguments.bind}:{Arguments.port}  (build: {Builds.State}, dir: {Builds.BuildDirectory})", flush=True)
    try:
        Server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        Inspection.Terminate()

if __name__ == "__main__":
    sys.exit(main())
