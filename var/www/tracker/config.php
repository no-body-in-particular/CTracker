<?php

define('DEVPATH', '/var/gps/');
define('DATABASE', '/var/gps/database.db');

/*
 * Start a session only when there is not one already.
 *
 * Every entry point starts a session, and most of them do it more than once: the page includes
 * lib.php and database.php, calls session_start() itself, and then reaches validateSession() or
 * isReadonly(), which start one again. PHP answers the second and third call with "Ignoring
 * session_start() because a session is already active", so a single page load wrote several
 * notices to the error log and every ajax poll repeated them.
 */
function start_session()
{
    if (PHP_SESSION_NONE === session_status()) {
        session_start();
    }
}

?>