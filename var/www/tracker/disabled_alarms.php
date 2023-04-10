<?php

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

function check_alarms($code)
{
    return preg_match('/^[A-Za-z0-9 *\.\|]*/u', $code);
}

if (isset($_GET['alarms']) && (!check_alarms($_GET['alarms']) || strlen($_GET['alarms']) > 2000)) {
    echo 'Please set a valid alarms line.';

    exit();
}

$IMEI = $_GET['imei'];
$ACTION = $_GET['action'];
$EVENTS = $_GET['alarms'];

validateIMEI($IMEI);

function write_alarms($alarms): void
{
    $fn = DEVPATH.$GLOBALS['IMEI'].'.disabled-alarms.txt';
    $myfile = fopen($fn, 'w');
    if (!$myfile) {
        exit('Unable to open '.$fn);
    }
    fwrite($myfile, $alarms);
    fclose($myfile);
}

switch ($ACTION) {
    case 'write':
        if (isset($_GET['alarms'])) {
            write_alarms($EVENTS);
        }

        break;

    case 'read':
    default:
        echo file_get_contents(DEVPATH.$IMEI.'.disabled-alarms.txt');
}

?>