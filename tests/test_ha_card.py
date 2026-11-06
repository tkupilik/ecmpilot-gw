"""Render the shipped card against fresh, stale and missing diagnostic snapshots."""
from datetime import datetime, timezone
import unittest

from jinja2 import Environment, StrictUndefined
import yaml

from test_udp import ROOT


class HaCardTests(unittest.TestCase):
    def setUp(self):
        self.wall = 1762450000.0
        self.prefix = 'sensor.ecmpilot_gateway_'
        self.age_entity = self.prefix + 'battery_data_age'
        self.gateway_entity = 'binary_sensor.ecmpilot_gateway_charge_limit_enabled'
        self.soc = self.prefix + 'battery_state_of_charge'
        self.outside = self.prefix + 'outside_temperature'
        self.values = {
            self.soc: '57', self.outside: '5.4',
            self.prefix + 'estimated_range': '214',
            self.prefix + '12v_battery_state_of_charge': '81',
            self.prefix + 'bsi_state': 'standby',
            self.prefix + 'reported_preconditioning_state': 'off',
            self.prefix + 'charging_state': 'waiting',
            self.prefix + 'charging_mode': 'delayed',
            'binary_sensor.ecmpilot_gateway_charging_cable': 'on',
            self.prefix + 'delayed_charging_start': '03:00',
            self.prefix + 'vehicle_locking_state': 'locked',
            self.age_entity: '0',
            self.gateway_entity: 'off',
        }
        self.attrs = {'age_s': dict.fromkeys((
            'soc_pct', 'range_km', 'outside_c', 'aux_battery_soc_pct',
            'bsi_state', 'climate_state', 'charge_state', 'charge_mode',
            'charge_connected', 'charge_start_local', 'locking_state'), 0),
            'received_at': self.wall}
        env = Environment(undefined=StrictUndefined)
        env.globals.update(
            states=lambda entity: self.values.get(entity, 'unknown'),
            has_value=lambda entity: self.values.get(entity, 'unknown') not in ('unknown', 'unavailable'),
            state_attr=lambda entity, key: self.attrs.get(key) if entity == self.age_entity else None,
            now=lambda: datetime.fromtimestamp(self.wall, timezone.utc),
            as_timestamp=lambda value: value.timestamp() if isinstance(value, datetime) else float(value),
            is_state=lambda entity, value: self.values.get(entity) == value,
            is_number=lambda value: self.is_number(value),
        )
        card = yaml.safe_load((ROOT / 'docs/home-assistant/vehicle-card.yaml').read_text(encoding='utf-8'))
        self.template = env.from_string(card['content'])

    @staticmethod
    def is_number(value):
        try:
            float(value)
            return True
        except (ValueError, TypeError):
            return False

    def test_unchanged_values_stay_fresh_then_age_without_new_messages(self):
        fresh = self.template.render()
        self.assertIn('Data právě teď', fresh)
        self.assertIn('Odloženo', fresh)
        self.wall += 301
        stale = self.template.render()
        self.assertIn('Data před 5 min', stale)
        self.assertIn('<font color="#8b929a" size="7">', stale)
        # The SOC value did not change, but a real new observation renews it.
        self.attrs['received_at'] = self.wall
        self.attrs['age_s'] = dict.fromkeys(self.attrs['age_s'], 301)
        self.attrs['age_s']['soc_pct'] = 0
        renewed = self.template.render()
        self.assertIn('Data právě teď', renewed)
        self.assertIn('<font size="7">', renewed)
        self.assertIn('<font color="#8b929a">Okolní teplota 5.4 °C</font>', renewed)

    def test_missing_diagnostics_do_not_hide_available_values_or_claim_freshness(self):
        self.attrs = {}
        text = self.template.render()
        self.assertIn('Data nejsou k dispozici', text)
        self.assertIn('<strong>57</strong>', text)
        self.assertIn('<font color="#8b929a" size="7">', text)

    def test_age_clock_uses_received_timestamp_even_if_diagnostic_state_changes(self):
        self.attrs['age_s']['soc_pct'] = 120
        self.wall += 60
        self.values[self.age_entity] = 'unavailable'
        self.assertIn('Data před 3 min', self.template.render())
        self.attrs['age_s']['soc_pct'] = None
        self.assertIn('Data nejsou k dispozici', self.template.render())

    def test_gateway_disconnect_does_not_show_cached_values_as_online(self):
        self.values = dict.fromkeys(self.values, 'unavailable')
        text = self.template.render()
        self.assertIn('GW nedostupná', text)
        self.assertIn('Data nejsou k dispozici', text)
        self.assertNotIn('<strong>57</strong>', text)

    def test_gateway_online_without_any_vehicle_observations(self):
        self.values = dict.fromkeys(self.values, 'unavailable')
        self.attrs = {}
        for enabled in ('off', 'on'):
            with self.subTest(charge_limit_enabled=enabled):
                self.values[self.gateway_entity] = enabled
                text = self.template.render()
                self.assertIn('GW online', text)
                self.assertNotIn('GW nedostupná', text)
                self.assertIn('BSI nedostupné', text)
                self.assertIn('Data nejsou k dispozici', text)
                self.assertNotIn('<strong>57</strong>', text)


if __name__ == '__main__':
    unittest.main()
