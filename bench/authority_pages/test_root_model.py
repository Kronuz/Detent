"""Tests option 2's relay and agent rules on root_model.py: python3 -I bench/authority_pages/test_root_model.py.

With ROOT_MODEL_RULES=strip (the earlier rules) the divergence tests fail: the agent publishes a
relay's wrong records.
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest

_spec = importlib.util.spec_from_file_location(
    "root_model", pathlib.Path(__file__).with_name("root_model.py")
)
assert _spec is not None and _spec.loader is not None
m = importlib.util.module_from_spec(_spec)
sys.modules["root_model"] = m
_spec.loader.exec_module(m)


def keys(first: int, last: int) -> list[str]:
    return [f"k{i % 3}" for i in range(first, last + 1)]


class Rules(unittest.TestCase):
    def test_a_correct_relay_publishes_its_head(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        auth.commit(*keys(5, 7))
        agent = m.Agent()
        self.assertTrue(agent.take(m.Relay(auth).answer(0), 1.0))
        self.assertEqual((agent.verified, agent.position), (4, 7))
        self.assertEqual(agent.state, m.applied({}, auth.changes))
        self.assertFalse(agent.unverified)

    def test_a_diverged_relay_forwards_the_root_and_the_agent_refuses(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 10))
        auth.pass_root()
        agent = m.Agent()
        self.assertFalse(agent.take(m.Relay(auth, wrong_at=9).answer(0), 1.0))
        self.assertEqual(
            (agent.position, agent.failures, agent.unverified), (0, 1, True)
        )
        self.assertTrue(agent.repair(m.Relay(auth), 2.0))
        self.assertEqual(agent.state, m.applied({}, auth.changes))

    def test_a_wrong_tail_already_published_stays_until_the_repair(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        auth.commit(*keys(5, 8))
        wrong = m.Relay(auth, wrong_at=6)
        agent = m.Agent()
        self.assertTrue(
            agent.take(wrong.answer(0), 1.0)
        )  # verified at 4, 5..8 unverified
        auth.pass_root()
        auth.commit(*keys(9, 10))
        self.assertFalse(agent.take(wrong.answer(agent.position), 2.0))
        self.assertEqual(
            (agent.position, agent.failures), (8, 1)
        )  # detection, not regression
        self.assertTrue(agent.repair(m.Relay(auth), 3.0))
        self.assertEqual((agent.verified, agent.position), (8, 10))
        self.assertEqual(agent.state, m.applied({}, auth.changes))

    def test_a_rootless_answer_publishes_nothing(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 5))
        agent = m.Agent()
        self.assertFalse(agent.take(m.Relay(auth).answer(0), 1.0))
        self.assertTrue(agent.unverified)

    def test_a_relay_that_loses_roots_trips_the_bound(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        relay = m.Relay(
            auth, roots_until=4
        )  # keeps sending root 4: re-checking it resets nothing
        agent = m.Agent()
        self.assertTrue(agent.take(relay.answer(0), 1.0))
        for second in range(2, 20):
            auth.commit(*keys(len(auth.changes) + 1, len(auth.changes) + 2))
            auth.pass_root()
            published = agent.take(relay.answer(agent.position), float(second))
            # The clock starts at the first unverified publish, at second 2.
            self.assertEqual(published, second - 2.0 <= m.BOUND_S)
        self.assertTrue(agent.unverified)
        self.assertEqual(agent.verified, 4)


class Clock(unittest.TestCase):
    def test_an_idle_namespace_publishes_its_first_write_at_once(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        relay = m.Relay(auth)
        agent = m.Agent()
        self.assertTrue(agent.take(relay.answer(0), 1.0))
        self.assertFalse(agent.take(relay.answer(4), 30.0))  # idle: nothing unverified
        self.assertFalse(agent.unverified)
        auth.commit(*keys(5, 6))  # the first write after idling, before its root
        self.assertTrue(agent.take(relay.answer(4), 40.0))
        self.assertEqual((agent.position, agent.unverified), (6, False))

    def test_a_stalled_builder_with_writes_trips_at_the_bound(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        relay = m.Relay(auth)
        agent = m.Agent()
        self.assertTrue(agent.take(relay.answer(0), 1.0))
        for second in range(5, 25):  # writes continue, roots stop
            auth.commit(f"k{second % 3}")
            published = agent.take(relay.answer(agent.position), float(second))
            self.assertEqual(published, second - 5.0 <= m.BOUND_S)
        self.assertTrue(agent.unverified)
        auth.pass_root()  # roots resume: it catches up in one answer
        self.assertTrue(agent.take(relay.answer(agent.position), 25.0))
        self.assertEqual((agent.position, agent.unverified), (len(auth.changes), False))

    def test_a_new_incarnation_is_invisible_until_its_first_root(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        relay = m.Relay(auth)
        agent = m.Agent()
        self.assertTrue(agent.take(relay.answer(0), 1.0))
        auth.recreate()
        auth.commit(*keys(1, 2))
        self.assertFalse(agent.take(relay.answer(0), 2.0))
        auth.pass_root()
        self.assertTrue(agent.take(relay.answer(0), 3.0))
        self.assertEqual((agent.incarnation, agent.position), (2, 2))

    def test_a_root_of_an_older_incarnation_is_ignored(self) -> None:
        auth = m.Authority()
        auth.commit(*keys(1, 4))
        auth.pass_root()
        stale = m.Relay(auth).answer(0)
        auth.recreate()
        auth.commit(*keys(1, 3))
        auth.pass_root()
        agent = m.Agent()
        self.assertTrue(agent.take(m.Relay(auth).answer(0), 1.0))
        self.assertFalse(agent.take(stale, 2.0))
        self.assertTrue(agent.unverified)


class Leader(unittest.TestCase):
    def test_the_pass_interval_is_a_minimum(self) -> None:
        self.assertEqual(m.pass_due_s(None, 5.0, 0.25), 5.0)
        self.assertEqual(m.pass_due_s(1.0, 9.0, 0.25), 9.0)  # idle: passes on arrival
        self.assertEqual(m.pass_due_s(1.0, 1.1, 0.25), 1.25)  # busy: waits the interval

    def test_a_result_of_an_old_term_is_never_proposed(self) -> None:
        leader = m.Leader()
        self.assertTrue(leader.offer(1, 10))
        leader.step_down()
        self.assertFalse(leader.offer(1, 20))  # finished after the step-down
        leader.elected()
        self.assertFalse(leader.offer(1, 30))  # built in the old term
        self.assertTrue(leader.offer(2, 40))
        self.assertEqual(leader.proposed, [(1, 10), (2, 40)])


if __name__ == "__main__":
    unittest.main()
