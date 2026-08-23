#!/usr/bin/env python3

import dataclasses
import unittest


@dataclasses.dataclass
class DurableState:
    enabled: bool = True
    cleanup_bit: bool = False
    publish_may_have_reached: bool = False
    wal_pending: bool = False
    snapshot_pending: bool = True


@dataclasses.dataclass
class RuntimeState:
    durable: DurableState
    consent_blocked: bool = False
    cleanup_pending: bool = False
    broker_retained: bool = False
    tombstones: int = 0
    data_publishes: int = 0

    def begin_crash_publish(self, marker_write_ok: bool, broker_accepts: bool) -> bool:
        if self.consent_blocked or not self.durable.enabled or not marker_write_ok:
            return False
        self.durable.publish_may_have_reached = True
        self.data_publishes += 1
        if broker_accepts:
            self.broker_retained = True
        return True

    def opt_out(self, wal_write_ok: bool, main_write_ok: bool) -> None:
        self.consent_blocked = True
        self.cleanup_pending = True
        if wal_write_ok:
            self.durable.wal_pending = True
        if main_write_ok:
            self.durable.enabled = False
            self.durable.cleanup_bit = True

    def retry_persistence_offline(self, wal_write_ok: bool, main_write_ok: bool) -> None:
        if not self.cleanup_pending:
            return
        if wal_write_ok:
            self.durable.wal_pending = True
        if main_write_ok:
            self.durable.enabled = False
            self.durable.cleanup_bit = True

    def tombstone(self, broker_ack: bool, snapshot_clear_ok: bool, main_clear_ok: bool, wal_clear_ok: bool) -> bool:
        if not self.cleanup_pending or not self.durable.cleanup_bit:
            return False
        self.tombstones += 1
        if not broker_ack:
            return False
        self.broker_retained = False
        if not snapshot_clear_ok:
            return False
        self.durable.snapshot_pending = False
        # Clear or prove absence of the WAL before dropping the legacy marker.
        if not wal_clear_ok:
            return False
        self.durable.wal_pending = False
        if not main_clear_ok:
            return False
        self.durable.enabled = False
        self.durable.cleanup_bit = False
        self.durable.publish_may_have_reached = False
        self.cleanup_pending = False
        return True

    @classmethod
    def reboot_new(cls, durable: DurableState, broker_retained: bool) -> "RuntimeState":
        state = cls(durable=durable, broker_retained=broker_retained)
        state.cleanup_pending = durable.wal_pending or durable.cleanup_bit or (
            not durable.enabled and durable.publish_may_have_reached
        )
        state.consent_blocked = not durable.enabled or state.cleanup_pending
        if not durable.enabled and not state.cleanup_pending:
            durable.snapshot_pending = False
        return state

    @classmethod
    def reboot_old(cls, durable: DurableState, broker_retained: bool) -> "RuntimeState":
        return cls(durable=durable, consent_blocked=not durable.enabled, broker_retained=broker_retained)


class UsageTelemetryCleanupLifecycleTest(unittest.TestCase):
    def test_crash_publish_is_blocked_until_marker_is_verified(self):
        state = RuntimeState(DurableState())
        self.assertFalse(state.begin_crash_publish(False, True))
        self.assertEqual(state.data_publishes, 0)
        self.assertFalse(state.broker_retained)
        self.assertTrue(state.durable.snapshot_pending)

    def test_crash_captured_while_disabled_does_not_create_network_cleanup(self):
        durable = DurableState(enabled=False, snapshot_pending=True)
        state = RuntimeState.reboot_new(durable, broker_retained=False)
        self.assertFalse(state.cleanup_pending)
        self.assertTrue(state.consent_blocked)
        self.assertFalse(state.durable.snapshot_pending)
        self.assertEqual(state.tombstones, 0)

    def test_puback_loss_then_old_firmware_opt_out_is_cleaned_after_upgrade(self):
        state = RuntimeState(DurableState())
        self.assertTrue(state.begin_crash_publish(True, True))
        old = RuntimeState.reboot_old(state.durable, state.broker_retained)
        old.opt_out(wal_write_ok=False, main_write_ok=True)
        upgraded = RuntimeState.reboot_new(old.durable, old.broker_retained)
        self.assertTrue(upgraded.cleanup_pending)
        self.assertTrue(upgraded.tombstone(True, True, True, True))
        self.assertFalse(upgraded.broker_retained)
        self.assertFalse(upgraded.durable.snapshot_pending)

    def test_wal_only_opt_out_never_authorizes_tombstone_or_safe_downgrade(self):
        state = RuntimeState(DurableState())
        state.opt_out(wal_write_ok=True, main_write_ok=False)
        self.assertFalse(state.tombstone(True, True, True, True))
        old = RuntimeState.reboot_old(state.durable, state.broker_retained)
        self.assertFalse(old.consent_blocked)

        state.retry_persistence_offline(wal_write_ok=False, main_write_ok=True)
        old_after_retry = RuntimeState.reboot_old(state.durable, state.broker_retained)
        self.assertTrue(old_after_retry.consent_blocked)

    def test_correlated_write_failure_retries_without_network(self):
        state = RuntimeState(DurableState())
        state.opt_out(wal_write_ok=False, main_write_ok=False)
        self.assertTrue(state.consent_blocked)
        self.assertFalse(state.tombstone(True, True, True, True))
        self.assertTrue(state.durable.snapshot_pending)
        state.retry_persistence_offline(wal_write_ok=True, main_write_ok=True)
        self.assertTrue(state.tombstone(True, True, True, True))

    def test_main_record_alone_still_requires_verified_wal_clear(self):
        durable = DurableState(
            enabled=False,
            cleanup_bit=True,
            publish_may_have_reached=True,
            wal_pending=False,
            snapshot_pending=True,
        )
        state = RuntimeState(
            durable=durable,
            consent_blocked=True,
            cleanup_pending=True,
            broker_retained=True,
        )
        self.assertFalse(state.tombstone(True, True, True, False))
        self.assertTrue(state.cleanup_pending)
        self.assertTrue(state.tombstone(True, True, True, True))
        self.assertFalse(state.cleanup_pending)

    def test_cleanup_evidence_clears_only_after_every_verified_step(self):
        for broker_ack, snapshot_ok, main_ok, wal_ok in (
            (False, True, True, True),
            (True, False, True, True),
            (True, True, False, True),
            (True, True, True, False),
        ):
            with self.subTest(
                broker_ack=broker_ack,
                snapshot_ok=snapshot_ok,
                main_ok=main_ok,
                wal_ok=wal_ok,
            ):
                durable = DurableState(
                    enabled=False,
                    cleanup_bit=True,
                    publish_may_have_reached=True,
                    wal_pending=True,
                    snapshot_pending=True,
                )
                state = RuntimeState(
                    durable=durable,
                    consent_blocked=True,
                    cleanup_pending=True,
                    broker_retained=True,
                )
                self.assertFalse(state.tombstone(broker_ack, snapshot_ok, main_ok, wal_ok))
                self.assertTrue(state.cleanup_pending)


if __name__ == "__main__":
    unittest.main()
