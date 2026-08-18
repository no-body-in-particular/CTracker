<?php

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

$IMEI = $_GET['imei'];
$BEGIN = $_GET['begin'];
$END = $_GET['end'] ?: PHP_INT_MAX;

//without this the raw device log - every gps fix, cell id and wifi mac - was readable
//by anyone who knew the imei, with no session and no credentials
validateIMEI($IMEI);

read_fordates(DEVPATH.$IMEI.'.log.txt', 0);

?>