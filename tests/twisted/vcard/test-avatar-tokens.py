
"""
Test GetAvatarTokens() and GetKnownAvatarTokens().
"""

from twisted.words.xish import domish

from servicetest import unwrap, assertEquals
from gabbletest import exec_test, make_result_iq
import ns
import constants as cs

def make_presence(jid, sha1sum):
    p = domish.Element((None, 'presence'))
    p['from'] = jid
    p['to'] = 'test@localhost/Resource'
    x = p.addElement((ns.VCARD_TEMP_UPDATE, 'x'))
    x.addElement('photo', content=sha1sum)
    return p

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', to=None, query_ns=ns.ROSTER,
            query_name='query')

    result = make_result_iq(stream, event.stanza)
    item = result.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = result.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'both'

    item = result.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'both'
    stream.send(result)

    stream.send(make_presence('amy@foo.com', 'SHA1SUM-FOR-AMY'))
    stream.send(make_presence('bob@foo.com', 'SHA1SUM-FOR-BOB'))
    stream.send(make_presence('che@foo.com', None))

    q.expect('dbus-signal', signal='AvatarUpdated')
    handles = conn.get_contact_handles_sync([
        'amy@foo.com', 'bob@foo.com', 'che@foo.com', 'daf@foo.com' ])

    h2asv = conn.Contacts.GetContactAttributes(handles, [cs.CONN_IFACE_AVATARS], False)
    assertEquals('SHA1SUM-FOR-AMY', h2asv[handles[0]][cs.ATTR_AVATAR_TOKEN])
    assertEquals('SHA1SUM-FOR-BOB', h2asv[handles[1]][cs.ATTR_AVATAR_TOKEN])
    assertEquals('', h2asv[handles[2]][cs.ATTR_AVATAR_TOKEN])
    assertEquals(None, h2asv[handles[3]].get(cs.ATTR_AVATAR_TOKEN))

    tokens = unwrap(conn.Avatars.GetKnownAvatarTokens(handles))
    tokens = sorted(tokens.items())
    assert tokens == [(2, 'SHA1SUM-FOR-AMY'), (3, 'SHA1SUM-FOR-BOB'), (4, u'')]

if __name__ == '__main__':
    exec_test(test)
