<?php

ini_set('zlib.output_compression', 1);
ini_set('session.cookie_lifetime', 864000);
ini_set('session.gc_maxlifetime', 864000);
//ob_start("ob_gzhandler");

include 'config.php';

$TZ = new DateTimeZone('UTC');

function check_imei($code)
{
    return preg_match('/^[\p{L}\p{N}]{16,16}$/u', $code);
}

function check_unixdate($code)
{
    return preg_match('/^[\p{L}\p{N}]{1,20}$/u', $code);
}

function datetotimestamp($dt)
{
    $d = DateTime::createFromFormat('Y-m-d\TH:i:s+', $dt, $GLOBALS['TZ']);
    if (false === $d) {
        return 0;
    }

    return $d->getTimestamp();
}

function read_fordates($file_path, $oneline): void
{
    if (!(check_unixdate($GLOBALS['BEGIN']) && check_unixdate($GLOBALS['END']))) {
        exit();
    }

    passthru('./date_grep '.$file_path.' '.$GLOBALS['BEGIN'].' '.$GLOBALS['END']);
}

function read_last_line($file_path)
{
    $output = [];
    $ret = 0;
    exec('tac '.$file_path." | grep -m 1 '[^[:blank:]]'", $output, $ret);

    return $output[0];
}

function multiexplode($delimiters, $string)
{
    $ready = str_replace($delimiters, $delimiters[0], $string);

    return explode($delimiters[0], $ready);
}

function redirect($url, $statusCode = 303): void
{
    header('Location: '.$url, true, $statusCode);

    exit();
}

function validateEmail($email)
{
    return preg_match_all('/^(?!(?:(?:\x22?\x5C[\x00-\x7E]\x22?)|(?:\x22?[^\x5C\x22]\x22?)){255,})(?!(?:(?:\x22?\x5C[\x00-\x7E]\x22?)|(?:\x22?[^\x5C\x22]\x22?)){65,}@)(?:(?:[\x21\x23-\x27\x2A\x2B\x2D\x2F-\x39\x3D\x3F\x5E-\x7E]+)|(?:\x22(?:[\x01-\x08\x0B\x0C\x0E-\x1F\x21\x23-\x5B\x5D-\x7F]|(?:\x5C[\x00-\x7F]))*\x22))(?:\.(?:(?:[\x21\x23-\x27\x2A\x2B\x2D\x2F-\x39\x3D\x3F\x5E-\x7E]+)|(?:\x22(?:[\x01-\x08\x0B\x0C\x0E-\x1F\x21\x23-\x5B\x5D-\x7F]|(?:\x5C[\x00-\x7F]))*\x22)))*@(?:(?:(?!.*[^.]{64,})(?:(?:(?:xn--)?[a-z0-9]+(?:-[a-z0-9]+)*\.){1,126}){1,}(?:(?:[a-z][a-z0-9]*)|(?:(?:xn--)[a-z0-9]+))(?:-[a-z0-9]+)*)|(?:\[(?:(?:IPv6:(?:(?:[a-f0-9]{1,4}(?::[a-f0-9]{1,4}){7})|(?:(?!(?:.*[a-f0-9][:\]]){7,})(?:[a-f0-9]{1,4}(?::[a-f0-9]{1,4}){0,5})?::(?:[a-f0-9]{1,4}(?::[a-f0-9]{1,4}){0,5})?)))|(?:(?:IPv6:(?:(?:[a-f0-9]{1,4}(?::[a-f0-9]{1,4}){5}:)|(?:(?!(?:.*[a-f0-9]:){5,})(?:[a-f0-9]{1,4}(?::[a-f0-9]{1,4}){0,3})?::(?:[a-f0-9]{1,4}(?::[a-f0-9]{1,4}){0,3}:)?)))?(?:(?:25[0-5])|(?:2[0-4][0-9])|(?:1[0-9]{2})|(?:[1-9]?[0-9]))(?:\.(?:(?:25[0-5])|(?:2[0-4][0-9])|(?:1[0-9]{2})|(?:[1-9]?[0-9]))){3}))\]))$/iD', $email);
}

function validateUsername($code)
{
    return preg_match_all('/^[A-Za-z0-9\_\-]{1,32}$/u', $code);
}

function validateName($code)
{
    return preg_match_all('/^[A-Za-z0-9\_\-\ ]{1,32}$/u', $code);
}

function validatePassword($pwd)
{
   return preg_match_all('/^\S*(?=\S{8,31})(?=\S*[a-z])(?=\S*[A-Z])(?=\S*[\d])\S*$/', $pwd);
}

function compareCaptcha($v1,$v2){
   return strtolower($v1) === strtolower($v2);
}

?>
