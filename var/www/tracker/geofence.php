<?php

include_once 'lib.php';
include_once 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    echo 'Please accuire a valid device link.';

    exit();
}

function check_fence($code)
{
    // anchored with $ and the D modifier so the whole value has to match. without them a
    // valid prefix let anything through after it - including a newline, which write_fence()
    // joins with "\n" and so becomes an extra fence line.
    // the name accepts space and dot as well as word characters and dash, and the two
    // coordinates accept a leading minus, which the previous pattern rejected outright.
    // the optional tenth field is the folder, same character set and length as the name.
    // a line without one is a fence in the default folder, which is every fence written
    // before folders existed - so the group is optional rather than the field being added.
    return preg_match('/^-*[\-0-9][0-9]:-*[\-0-9][0-9],-*[\-0-9][0-9]:-*[0-9][0-9],[0-9],[0-9],-?[0-9\\.]*,-?[0-9\\.]*,[0-9]*,[0-9]*,[\w .\-]{0,31}(,[\w .\-]{0,31})?$/uD', $code);
}

/*
 * The list of switched-off folders, as stored in <imei>.disabled-fences.txt: folder names
 * separated by commas, or a single * for all of them. Anchored, and restricted to the same
 * characters a folder name allows, because refreshFolders() puts this value into the page.
 */
function check_folders($code)
{
    return preg_match('/^(\*|[\w .\-]{0,31}(,[\w .\-]{0,31})*)$/uD', $code);
}

if (isset($_GET['folders']) && (!check_folders($_GET['folders']) || strlen($_GET['folders']) > 2000)) {
    echo 'Please set a valid folder list.';

    exit();
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
$FOLDERS = $_GET['folders'] ?? '';

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

function disabled_folders_file(): string
{
    return DEVPATH.$GLOBALS['IMEI'].'.disabled-fences.txt';
}

function write_disabled_folders($folders): void
{
    $fn = disabled_folders_file();
    $myfile = fopen($fn, 'w');

    if (!$myfile) {
        exit('Unable to open '.$fn);
    }

    fwrite($myfile, $folders);
    fclose($myfile);
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

    case 'setfolders':
        if (isReadonly()) {
            exit();
        }

        if (isset($_GET['folders'])) {
            write_disabled_folders($FOLDERS);
        }

        break;

    case 'folders':
        // absent file means nothing is switched off, which is how a device with no folders
        // set up behaves and the direction that keeps a curfew enforced
        if (is_file(disabled_folders_file())) {
            echo file_get_contents(disabled_folders_file());
        }

        break;

    case 'read':
    default:
        echo file_get_contents(DEVPATH.$IMEI.'.fence.txt');
}

?>

