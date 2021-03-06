"""
Test that EnsureChannel works for MUCs, particularly in the case when there
are several pending requests for the same MUC.
"""

from gabbletest import exec_test, make_muc_presence
from servicetest import (call_async, EventPattern, assertContains,
        assertEquals)
import constants as cs

def test(q, bus, conn, stream):
    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    jids = ['chat@conf.localhost', 'chien@conf.localhost']

    test_create_ensure(q, conn, bus, stream, jids[0])
    test_ensure_ensure(q, conn, bus, stream, jids[1])

def test_create_ensure(q, conn, bus, stream, room_jid):
    # Call both Create and Ensure for the same channel.
    call_async(q, conn.Requests, 'CreateChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_ID: room_jid,
           })
    call_async(q, conn.Requests, 'EnsureChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_ID: room_jid,
           })

    mc, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChangedDetailed'),
        EventPattern('stream-presence', to=('%s/test' % room_jid)))
    added, removed, local_pending, remote_pending, details = mc.args

    assert added == [], mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert len(remote_pending) == 1, mc.args

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test'))

    mc = q.expect('dbus-signal', signal='MembersChangedDetailed')
    added, removed, local_pending, remote_pending, details = mc.args

    assert len(added) == 2, mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert remote_pending == [], mc.args

    members = conn.inspect_contacts_sync(added)
    members.sort()
    assert members == ['%s/bob' % room_jid, '%s/test' % room_jid], members

    create_event, ensure_event = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-return', method='EnsureChannel'))

    assert len(create_event.value) == 2
    c_path, c_props = create_event.value

    assert len(ensure_event.value) == 3
    yours, e_path, e_props = ensure_event.value

    assert c_path == e_path, (c_path, e_path)
    assert c_props == e_props, (c_props, e_props)

    assert not yours

    assertContains('text/plain', c_props[cs.SUPPORTED_CONTENT_TYPES])
    assertEquals(0, c_props[cs.MESSAGE_PART_SUPPORT_FLAGS])
    assertEquals(
            cs.DELIVERY_REPORTING_SUPPORT_FLAGS_RECEIVE_FAILURES |
            cs.DELIVERY_REPORTING_SUPPORT_FLAGS_RECEIVE_SUCCESSES,
            c_props[cs.DELIVERY_REPORTING_SUPPORT])


def test_ensure_ensure(q, conn, bus, stream, room_jid):
    # Call Ensure twice for the same channel.
    call_async(q, conn.Requests, 'EnsureChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_ID: room_jid,
           })
    call_async(q, conn.Requests, 'EnsureChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_ID: room_jid,
           })

    mc, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChangedDetailed'),
        EventPattern('stream-presence', to=('%s/test' % room_jid)))
    added, removed, local_pending, remote_pending, details = mc.args

    assert added == [], mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert len(remote_pending) == 1, mc.args

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test'))

    mc = q.expect('dbus-signal', signal='MembersChangedDetailed')
    added, removed, local_pending, remote_pending, details = mc.args

    assert len(added) == 2, mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert remote_pending == [], mc.args

    members = conn.inspect_contacts_sync(added)
    members.sort()
    assert members == ['%s/bob' % room_jid, '%s/test' % room_jid], members

    # We should get two EnsureChannel returns
    es = []
    while len(es) < 2:
        e = q.expect('dbus-return', method='EnsureChannel')
        es.append(e)

    e1, e2 = es

    assert len(e1.value) == 3
    yours1, path1, props1 = e1.value

    assert len(e2.value) == 3
    yours2, path2, props2 = e2.value

    # Exactly one Ensure should get Yours=True.
    assert (yours1 == (not yours2))

    assert path1 == path2, (path1, path2)
    assert props1 == props2, (props1, props2)


if __name__ == '__main__':
    exec_test(test)

