import sys,struct,unittest
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from build_order import decode_build_order, WINDOW_MS


def trace(commands, names=None, final=12000):
    initial=bytearray(664);struct.pack_into('<I',initial,0,100)
    frames=[]
    for tick,data in commands:
        frames.append(((tick,0,0,0),((tick,0,0,data),),(tick,0,0,0)))
    return SimpleNamespace(initial=bytes(initial),frames=frames,final=(final,0,0,0),bootstrap=None,player_names=names,map_name='test-map')

class BuildOrderTests(unittest.TestCase):
    def test_both_players_and_production_variants_with_time(self):
        report=decode_build_order(trace([(100,struct.pack('<HHi',400,1,15)),(105,struct.pack('<HHi',401,0,0))],('James','상대방')+('',)*6))
        self.assertEqual(report.players,{0:'James',1:'상대방'})
        self.assertEqual([(e.time,e.player,e.name) for e in report.events],[('0:00.000',1,'Gnome Slave'),('0:00.150',0,'Peasant')])
        self.assertEqual([e.category for e in report.events],['생산','생산'])

    def test_build_queue_coordinates_and_research_names(self):
        report=decode_build_order(trace([(100,struct.pack('<HHhhi',170,0,128,256,48)),(101,struct.pack('<HHhhi',5170,1,300,400,64)),(102,struct.pack('<HHi',500,0,0)),(103,struct.pack('<HHi',500,1,2))]))
        self.assertEqual(report.events[0].name,'Barrack')
        self.assertIn('(128, 256)',report.events[0].detail)
        self.assertIn('예약',report.events[1].detail)
        self.assertEqual(report.events[2].category,'건물 업그레이드')
        self.assertEqual(report.events[2].name,'Upgrade to Castle')
        self.assertEqual(report.events[3].name,'Make Steel Weapons')

    def test_five_minute_boundary_and_tick_interval(self):
        p=struct.pack('<HHi',400,0,0)
        report=decode_build_order(trace([(100,p),(5100,p),(5101,p)]),60)
        self.assertEqual(len(report.events),2)
        self.assertEqual(report.events[-1].elapsed_ms,WINDOW_MS)
        self.assertEqual(report.duration_ms,WINDOW_MS)
        self.assertEqual(report.players,{0:'P1'})

    def test_no_orders_does_not_invent_players_or_completion(self):
        report=decode_build_order(trace([(100,b'\x62\x00')],final=110))
        self.assertEqual(report.events,())
        self.assertEqual(report.players,{})
        self.assertEqual(report.duration_ms,300)

    def test_invalid_relevant_packets_rejected(self):
        for packet in [struct.pack('<HH',400,0),struct.pack('<HHi',400,8,0),struct.pack('<HHi',500,0,148),struct.pack('<HHhhi',170,0,0,0,-1)]:
            with self.subTest(packet=packet):
                with self.assertRaises(ValueError):decode_build_order(trace([(100,packet)]))
        for interval in (0,1001,True):
            with self.assertRaises(ValueError):decode_build_order(trace([]),interval)

if __name__=='__main__':unittest.main()

class PopulationTests(unittest.TestCase):
    def test_population_uses_all_command_ordinals_and_preserves_overcap(self):
        t=trace([(100,b'\x62\x00'),(101,struct.pack('<HHi',400,0,0)),(102,struct.pack('<HHi',500,0,0))])
        result=decode_build_order(t,population=[[1,12,10],[2,12,20]])
        self.assertEqual([e.supply for e in result.events],['12/10','12/20'])
        self.assertEqual(decode_build_order(t).events[0].supply,'—')

    def test_invalid_population_is_rejected(self):
        t=trace([(100,struct.pack('<HHi',400,0,0))])
        for values in ([[0,-1,10]],[[0,True,10]],[[0,2,10],[0,3,10]],[[1,2,10]],[[0,1,2,3]]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                decode_build_order(t,population=values)

    def test_sidecar_recovery_discards_samples_after_retained_frames(self):
        import tempfile
        from engine import read_population
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'game.partial.complete'
            sidecar=Path(folder)/'game.partial.population'
            t=trace([(100,struct.pack('<HHi',400,1,0))])
            sidecar.write_bytes(b'KUFPOP01'+struct.pack('<5I',0,100,1,7,10)+struct.pack('<5I',1,101,1,8,10)+b'cut')
            self.assertEqual(read_population(source,t),[[0,7,10]])
            sidecar.write_bytes(b'KUFPOP01'+struct.pack('<5I',0,100,0,7,10))
            with self.assertRaises(ValueError):read_population(source,t)
