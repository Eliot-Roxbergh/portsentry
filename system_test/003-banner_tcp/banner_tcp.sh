#!/bin/sh
. ./testlib.sh

verbose "expect connect to tcp localhost:11 w/ banner"
if ! $TEST_DIR/portcon 11 tcp | grep -q "Some banner printed on port"; then
  err "Expected banner not found"
fi

verbose "expect attackalert block message"
if ! findInFile "^attackalert: Host 127.0.0.1 has been blocked" $PORTSENTRY_STDOUT; then
  err "Expected attackalert message not found"
fi

verbose "expect blocked tcp port"
if ! findInFile "Host: 127.0.0.1/127.0.0.1 Port: 11 TCP Blocked" $TEST_DIR/portsentry.blocked.tcp; then
  err "Expected blocked TCP port not found"
fi

verbose "expect history entry"
if ! findInFile ".*127\.0\.0\.1/127\.0\.0\.1 Port: 11 TCP Blocked" $TEST_DIR/portsentry.history; then
  err "Expected history entry not found"
fi

if ! findInFile "^Unable to open ignore file .*/portsentry.ignore. Continuing without it" $PORTSENTRY_STDERR; then
  err "Expected block anyway message not found"
fi

verbose "Re-connect to tcp localhost:11"
nmap -sT -p11-11 localhost >/dev/null

verbose "expect already blocked message"
if ! findInFile "attackalert: Host: 127.0.0.1/127.0.0.1 is already blocked Ignoring" $PORTSENTRY_STDOUT; then
  err "Expected already blocked message not found"
fi

ok
