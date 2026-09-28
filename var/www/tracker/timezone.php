<?php

/*
 * The timezone a device's times are meant in.
 *
 * Fence hours and the times on the map used to have no single answer. The server evaluates
 * a fence window in UTC, while the map printed every position in whatever timezone the
 * browser happened to be in - so a curfew written "22:00" fired at midnight local, and two
 * people watching the same device from different countries read different times off the
 * same row and neither was told which.
 *
 * One zone per device settles it: the wearer's, because that is what a curfew means. The
 * server still works in UTC and is unchanged; this is the zone the page converts to and
 * from, so both of them see the wearer's clock whatever their own says.
 *
 * Stored as an IANA name ("Europe/Berlin"), never as an offset, so daylight saving is the
 * zone database's problem rather than something anyone adjusts twice a year.
 */

include_once 'lib.php';
include_once 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

/*
 * IANA zone names are letters, digits, and the few punctuation marks that appear in
 * "America/Argentina/Buenos_Aires", "Etc/GMT+3" and "America/Port-au-Prince". Anchored, and
 * deliberately narrow: this value is written into the page, so anything looser is a stored
 * scripting hole of the kind check_alarms() above had.
 */
function check_timezone($tz)
{
    return preg_match('#^[A-Za-z0-9_+/\-]{1,64}$#uD', $tz);
}

if (isset($_GET['zone']) && !check_timezone($_GET['zone'])) {
    echo 'Please set a valid timezone.';

    exit();
}

$IMEI = $_GET['imei'];
$ACTION = $_GET['action'] ?? '';
$ZONE = $_GET['zone'] ?? '';

validateIMEI($IMEI);

function timezone_file(): string
{
    return DEVPATH.$GLOBALS['IMEI'].'.timezone.txt';
}

function write_timezone($zone): void
{
    // A name that passes the pattern can still be one the zone database has never heard of,
    // and a device filed under a zone that does not exist would silently fall back to the
    // browser's - which is the bug this file exists to remove.
    if (!in_array($zone, DateTimeZone::listIdentifiers(), true)) {
        exit('Unknown timezone.');
    }

    $fn = timezone_file();
    $myfile = fopen($fn, 'w');

    if (!$myfile) {
        exit('Unable to open '.$fn);
    }

    fwrite($myfile, $zone);
    fclose($myfile);
}

switch ($ACTION) {
    case 'write':
        // same reasoning as disabled_alarms.php: a read-only share link must not be able to
        // move someone else's curfew by an hour
        if (isReadonly()) {
            exit();
        }

        if (isset($_GET['zone'])) {
            write_timezone($ZONE);
        }

        break;

    case 'read':
    default:
        $fn = timezone_file();

        // No file means nobody has chosen one. Answer with the server's own zone rather than
        // an empty string: it is the best guess available, it is the same answer for both
        // viewers, and it is what the times were already being compared against.
        echo is_file($fn) ? trim(file_get_contents($fn)) : date_default_timezone_get();
}

?>
