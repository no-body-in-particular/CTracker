<?php

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

function check_fence($code)
{
    return preg_match('/^-*[\-0-9][0-9]:-*[\-0-9][0-9],-*[\-0-9][0-9]:-*[0-9][0-9],[0-9],[0-9],[0-9\\.]*,[0-9\\.]*,[0-9]*,[0-9]*,[\w-]{0,31}/u', $code);
}

if (isset($_GET['fence']) && !check_fence($_GET['fence'])) {
    echo 'Please set a valid fence line.';

    exit();
}

$IMEI = $_GET['imei'];
$BEGIN = $_GET['begin'];
$END = $_GET['end'] ?: PHP_INT_MAX;
$ACTION = $_GET['action'];
$FENCE = $_GET['fence'];

validateSession();
validateIMEI($IMEI);

function read_fence($remove)
{
    $rows = file(DEVPATH.$GLOBALS['IMEI'].'.fence.txt');
    if (null === $rows || false === $rows) {
        return [];
    }

    foreach ($rows as $key => $row) {
        $rows[$key] = preg_replace('/\n$/', '', $rows[$key]);
        if (('' !== $remove && str_starts_with($row, $remove)) || strlen($row) < 3) {
            unset($rows[$key]);
        }
    }

    return $rows;
}

function write_fence($rows): void
{
    $fn = DEVPATH.$GLOBALS['IMEI'].'.fence.txt';
    $myfile = fopen($fn, 'w');
    
    if (!$myfile) {
        exit('Unable to open '.$fn);
    }

    fwrite($myfile, implode("\n", array_values(array_filter($rows))));
    fclose($myfile);
}

switch ($ACTION) {
    case 'write':
        if (isReadonly()) {
            exit();
        }

        if (isset($_GET['fence'])) {
            $arr = read_fence('');
            $arr[] = $FENCE;
            write_fence($arr);
        }

        break;

    case 'clear':
        if (isReadonly()) {
            exit();
        }

        write_fence([]);

        break;

    case 'remove':
        if (isReadonly()) {
            exit();
        }

        if (isset($_GET['fence'])) {
            $arr = read_fence($FENCE);
            echo count($arr);
            write_fence($arr);
        }

        break;

    case 'read':
    default:
        echo file_get_contents(DEVPATH.$IMEI.'.fence.txt');
}

?>

