<?php

ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

function check_events($code)
{
    return preg_match('/^[A-Za-z0-9 \*\.]*/u', $code);
}

if (isset($_GET['events']) && !check_events($_GET['events'])) {
    echo 'Please set a valid events line.';

    exit();
}

$IMEI = $_GET['imei'];
$ACTION = $_GET['action'];
$EVENTS = $_GET['events'];
validateIMEI($IMEI);

function write_events($events): void
{
    $fn = DEVPATH.$GLOBALS['IMEI'].'.disabled-events.txt';
    $myfile = fopen($fn, 'w');
    if (!$myfile) {
        exit('Unable to open '.$fn);
    }
    fwrite($myfile, $events);
    fclose($myfile);
}

switch ($ACTION) {
    case 'write':
        if (isReadonly()) {
            exit();
        }

        if (isset($_GET['events'])) {
            write_events($EVENTS);
        }

        break;

    case 'read':
    default:
        echo file_get_contents(DEVPATH.$IMEI.'.disabled-events.txt');
}

?>
