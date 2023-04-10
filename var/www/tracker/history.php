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
validateIMEI($IMEI);

read_fordates(DEVPATH.$IMEI.'.gps.txt', 0);

?>