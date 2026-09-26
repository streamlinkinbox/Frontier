#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('report',Path(__file__).resolve().parents[1]/'Build/ReportStartup.py')
r = importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
def row(event, time, commit=-1):
    return dict(event=event,since_main_ms=time,rss_bytes=100,peak_rss_bytes=200,private_commit_bytes=commit)
class StartupTests(unittest.TestCase):
    def test_overlap(self):
        self.assertEqual(r.union_ms([(0,5),(1,4),(2,9)]),9)
    def test_disjoint(self):
        self.assertEqual(r.union_ms([(0,5),(10,15)]),10)
    def test_nested_duplicate(self):
        self.assertEqual(r.union_ms([(0,10),(0,10),(2,3),(3,4)]),10)
    def test_empty(self):
        self.assertEqual(r.union_ms([]),0)
    def test_reversed(self):
        with self.assertRaises(ValueError):r.union_ms([(3,1)])
    def test_actual_worker_timestamps(self):
        rows=[row('TextureDecode:begin',87267.1),row('VulkanBringUp:begin',87271.6),row('CwbvhBuild:begin',87280),row('CwbvhBuild:end',91064.8),row('TextureDecode:end',92408.3)]
        phases,warnings=r.intervals(rows)
        self.assertAlmostEqual(r.union_ms([(a,b) for _,a,b in phases]),5141.2)
        self.assertEqual(len(warnings),1)
        text=r.render(rows)
        self.assertIn('5.141 s',text);self.assertIn('VulkanBringUp: incomplete',text)
        self.assertIn('NOT startup elapsed',text)
    def test_end_without_begin(self):
        self.assertEqual(len(r.intervals([row('A:end',2)])[1]),1)
    def test_repeated_phases(self):
        phases,warnings=r.intervals([row('A:begin',1),row('A:end',2),row('A:begin',3),row('A:end',4)])
        self.assertEqual(len(phases),2);self.assertFalse(warnings)
    def test_no_completion_inferred(self):
        self.assertIn('FirstPresentReturned: not recorded',r.render([row('main',0)]))
    def test_unknown_commit(self):
        self.assertEqual(r.memory(-1),'N/A')
    def test_matching_pid_and_ready(self):
        rows=[row('FrameLoopReady',1000),row('FirstPresentReturned',2000)]
        rows[0]['process_id']='123';text=r.render(rows)
        self.assertIn('Logged process ID(s): 123',text);self.assertIn('FirstPresentReturned: 2.000 s',text)
    def test_csv_compatibility_and_bad_input(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'input.csv'
            p.write_text('event,since_main_ms,phase_ms,rss_bytes,peak_rss_bytes,private_commit_bytes,payload_bytes\nmain,0,-1,100,200,-1,0\n')
            self.assertEqual(len(r.load(p)),1)
            p.write_text('wrong,header\n')
            with self.assertRaises(ValueError):r.load(p)
if __name__=='__main__':unittest.main()
