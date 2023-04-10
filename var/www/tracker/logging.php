<?php

include 'lib.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

$IMEI = $_GET['imei'];
$BEGIN = $_GET['begin'];
$END = $_GET['end'] ?: PHP_INT_MAX;

read_fordates(DEVPATH.$IMEI.'.log.txt', 0);

?>