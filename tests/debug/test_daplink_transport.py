"""DAPLink transport checks without connecting to hardware."""
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/nebulizer_debug'))
from transport import DapLink, open_daplink, list_daplink_probes
from pyocd.core.target import Target


class DapLinkTests(unittest.TestCase):
    def test_attach_options_and_exact_probe_selection(self):
        probe = MagicMock(unique_id='DAP_ABC123')
        with patch('pyocd.probe.cmsis_dap_probe.CMSISDAPProbe.get_all_connected_probes',
                   return_value=[probe]), patch('pyocd.core.session.Session.__init__', return_value=None) as init, \
             patch('pyocd.core.session.Session.open'), patch('transport.DapLink') as adapter:
            adapter.return_value.halted.return_value = False
            self.assertIs(open_daplink('DAP_ABC123', 1000), adapter.return_value)
            options = init.call_args.kwargs['options']
            self.assertEqual(options['connect_mode'], 'attach')
            self.assertEqual(options['frequency'], 1000000)
            self.assertEqual(options['target_override'], 'cortex_m')
            for key in ('auto_unlock', 'resume_on_disconnect', 'cache.enable_memory',
                        'cache.enable_register', 'cache.read_code_from_elf', 'pack.debug_sequences.enable'):
                self.assertFalse(options[key])
            self.assertTrue(options['no_config'])
            with self.assertRaises(RuntimeError):
                open_daplink('ABC', 1000)
            self.assertEqual(list_daplink_probes(), ['DAP_ABC123'])

    def test_open_failure_closes_session(self):
        probe = MagicMock(unique_id='DAP_A')
        with patch('pyocd.probe.cmsis_dap_probe.CMSISDAPProbe.get_all_connected_probes',
                   return_value=[probe]), patch('pyocd.core.session.Session.__init__', return_value=None), \
             patch('pyocd.core.session.Session.open', side_effect=IOError('USB lost')), \
             patch('pyocd.core.session.Session.close') as close:
            with self.assertRaises(IOError): open_daplink('DAP_A', 1000)
            close.assert_called_once()

    def test_real_word_transfer_flush_and_failure(self):
        session = MagicMock()
        link = DapLink(session)
        self.assertEqual(link.memory_write32(0x20000020, [7]), 4)
        session.target.write32.assert_called_once_with(0x20000020, 7)
        session.target.flush.assert_called_once()
        session.target.flush.side_effect = IOError('transfer fault')
        with self.assertRaises(IOError): link.memory_write8(0x20000030, [1, 2])
        for state in (Target.State.RUNNING, Target.State.SLEEPING):
            session.target.get_state.return_value = state
            self.assertFalse(link.halted())
        session.target.get_state.return_value = Target.State.HALTED
        self.assertTrue(link.halted())
        link.close()
        session.close.assert_called_once()


if __name__ == '__main__': unittest.main(verbosity=2)
