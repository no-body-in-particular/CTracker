<?php

/*
 * Serves one picture out of <imei>.images.db, or lists what the file holds.
 *
 * The container is a sequence of "CTIMG1 <unix_ts> <device_time> <bytes> <lat> <lon>\n"
 * headers each followed by exactly <bytes> of image and a newline, so this walks it by
 * reading a header and seeking over the payload rather than reading the file into memory -
 * the file grows without bound as pictures arrive and is not something to slurp.
 *
 *   image.php?imei=...&list=1   -> json index
 *   image.php?imei=...&ts=...   -> the image bytes
 */

include 'lib.php';
include 'database.php';

if (!isset($_GET['imei']) || !check_imei($_GET['imei'])) {
    http_response_code(400);
    exit('Please accuire a valid device link.');
}

$IMEI = $_GET['imei'];
validateIMEI($IMEI);

$path = DEVPATH.$IMEI.'.images.db';

if (!is_readable($path)) {
    http_response_code(404);
    exit('no pictures for this device');
}

$fp = fopen($path, 'rb');

if (false === $fp) {
    http_response_code(500);
    exit('could not open the picture store');
}

$wantList = isset($_GET['list']);
$wantTs = isset($_GET['ts']) ? $_GET['ts'] : '';

// A timestamp is the only thing addressable here, and it is only ever compared as a string
// of digits - nothing from the query string reaches the filesystem.
if (!$wantList && !preg_match('/^\d{1,20}$/', $wantTs)) {
    fclose($fp);
    http_response_code(400);
    exit('bad picture id');
}

$index = [];

while (!feof($fp)) {
    $line = fgets($fp, 256);

    if (false === $line) {
        break;
    }

    $line = rtrim($line, "\r\n");

    if ('' === $line) {
        continue;
    }

    $parts = preg_split('/\s+/', $line);

    // Anything that is not a header means the file has been damaged or truncated mid record.
    // There is no length to trust at that point, so stop rather than guess.
    if (count($parts) < 4 || 'CTIMG1' !== $parts[0] || !ctype_digit($parts[3])) {
        break;
    }

    $ts = $parts[1];
    $devtime = $parts[2];
    $len = (int) $parts[3];
    $lat = isset($parts[4]) ? (float) $parts[4] : 0.0;
    $lon = isset($parts[5]) ? (float) $parts[5] : 0.0;
    $offset = ftell($fp);

    if (!$wantList && $ts === $wantTs) {
        header('Content-Type: image/jpeg');
        header('Content-Length: '.$len);
        // the pictures are immutable once written, so let the browser keep them
        header('Cache-Control: private, max-age=86400');
        $sent = 0;

        while ($sent < $len && !feof($fp)) {
            $chunk = fread($fp, min(65536, $len - $sent));

            if (false === $chunk || '' === $chunk) {
                break;
            }

            echo $chunk;
            $sent += strlen($chunk);
        }

        fclose($fp);

        exit();
    }

    if ($wantList) {
        $index[] = ['ts' => $ts, 'taken' => $devtime, 'bytes' => $len, 'lat' => $lat, 'lon' => $lon];
    }

    // step over the payload and the newline that follows it
    if (-1 === fseek($fp, $offset + $len + 1, SEEK_SET)) {
        break;
    }
}

fclose($fp);

if ($wantList) {
    header('Content-Type: application/json');
    echo json_encode($index);

    exit();
}

http_response_code(404);
echo 'no such picture';

?>
