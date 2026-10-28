"""Serve the recovered references and the vehicle telemetry export route on one origin."""
from functools import partial
from http.server import ThreadingHTTPServer
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Source = Root / 'Experimental' / 'VehicleHmiPrototype' / 'Tools' / 'Serve.py'
Specification = spec_from_file_location('VehicleReferenceServer', Source)
VehicleServer = module_from_spec(Specification)
Specification.loader.exec_module(VehicleServer)


class ReferenceRequests(VehicleServer.Handler):
    def do_GET(self):
        if self.path == '/':
            self.send_response(302)
            self.send_header('Location', '/Experimental/FluidPath/')
            self.end_headers()
            return
        super().do_GET()


if __name__ == '__main__':
    ThreadingHTTPServer(('0.0.0.0', 8000), partial(ReferenceRequests, directory=str(Root))).serve_forever()
