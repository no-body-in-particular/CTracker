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

//as logging.php: this was readable without any authorisation at all
validateIMEI($IMEI);

read_fordates(DEVPATH.$IMEI.'.command-output.txt', 0);

?>