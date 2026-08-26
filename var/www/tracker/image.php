<?php

/*
 * Serves one picture out of <imei>.images.db, or lists what the file holds.
 *
 * The container is a sequence of "CTIMG2 <unix_ts> <device_time> <bytes> <lat> <lon>\n"
 * headers, each followed by the picture as 2 * <bytes> hex characters and a newline. This
 * walks it by reading a header and seeking over the payload rather than reading the file
 * into memory - it grows without bound as pictures arrive and is not something to slurp.
 * CTIMG1 is the older form with a raw payload and is still read.
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
    if (count($parts) < 4 || !ctype_digit($parts[3])
        || ('CTIMG2' !== $parts[0] && 'CTIMG1' !== $parts[0])) {
        break;
    }

    // the picture size is what the header states; a hex payload takes twice that on disk
    $hex = ('CTIMG2' === $parts[0]);

    $ts = $parts[1];
    $devtime = $parts[2];
    $len = (int) $parts[3];
    $lat = isset($parts[4]) ? (float) $parts[4] : 0.0;
    $lon = isset($parts[5]) ? (float) $parts[5] : 0.0;
    $onwire = $hex ? $len * 2 : $len;
    $offset = ftell($fp);

    if (!$wantList && $ts === $wantTs) {
        header('Content-Type: image/jpeg');
        header('Content-Length: '.$len);
        // the pictures are immutable once written, so let the browser keep them
        header('Cache-Control: private, max-age=86400');
        $sent = 0;

        while ($sent < $onwire && !feof($fp)) {
            // an even read size keeps hex pairs whole, so each chunk decodes on its own
            $chunk = fread($fp, min(65536, $onwire - $sent));

            if (false === $chunk || '' === $chunk) {
                break;
            }

            $sent += strlen($chunk);

            if ($hex) {
                // a short read could split a pair; carry the odd character to the next round
                if (1 === strlen($chunk) % 2) {
                    $carry = substr($chunk, -1);
                    $chunk = substr($chunk, 0, -1);
                    fseek($fp, -1, SEEK_CUR);
                    $sent -= 1;
                }

                $chunk = @hex2bin($chunk);

                if (false === $chunk) {
                    break;
                }
            }

            echo $chunk;
        }

        fclose($fp);

        exit();
    }

    if ($wantList) {
        $index[] = ['ts' => $ts, 'taken' => $devtime, 'bytes' => $len, 'lat' => $lat, 'lon' => $lon];
    }

    // step over the payload and the newline that follows it
    if (-1 === fseek($fp, $offset + $onwire + 1, SEEK_SET)) {
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
