<?php

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

$IMEI = $_GET['imei'];
$LINE = (isset($_POST['command']) ? $_POST['command'] : $_GET['command']).PHP_EOL;

validateIMEI($IMEI);
if (isReadonly()) {
    exit();
}

$fp = fopen(DEVPATH.$IMEI.'.command.txt', 'a'); //opens file in append mode
fwrite($fp, $LINE);
fclose($fp);

ob_start();

header('HTTP/1.1 204 NO CONTENT');

header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');
header('Expires: 0');

ob_end_flush();

?>