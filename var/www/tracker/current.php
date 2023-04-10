<?php

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    exit('Please accuire a valid device link.');
}

$IMEI = $_GET['imei'];
validateIMEI($IMEI);

echo read_last_line(DEVPATH.$IMEI.'.gps.txt')."\n";
echo file_get_contents(DEVPATH.$IMEI.'.status.txt', true);

?>