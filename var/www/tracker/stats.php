<?php

// display_startup_errors / error_reporting(E_ALL) were on here, printing php warnings - and the
// absolute file paths in them - straight into the response. errors belong in the log, not the page.
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
read_fordates(DEVPATH.$IMEI.'.stats.txt', 0);
?>