<?php

ini_set('zlib.output_compression', 1);
ini_set('session.cookie_lifetime', 864000);
ini_set('session.gc_maxlifetime', 864000);

// Session cookie hardening, applied here because lib.php is included before any session starts.
//  - HttpOnly keeps the cookie out of reach of javascript, so an XSS cannot read the session.
//  - SameSite=Lax stops another site from driving an authenticated request with the cookie
//    attached, which is a CSRF defence the app otherwise had none of.
//  - Secure is set on HTTPS so the cookie is never sent in clear over the port 80 binding.
//  - strict_mode makes PHP reject a session id it did not issue, closing session fixation.
ini_set('session.cookie_httponly', 1);
ini_set('session.cookie_samesite', 'Lax');
ini_set('session.use_strict_mode', 1);

if (!empty($_SERVER['HTTPS']) && 'off' !== $_SERVER['HTTPS']) {
    ini_set('session.cookie_secure', 1);
}
//ob_start("ob_gzhandler");

include_once 'config.php';

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

    passthru('./date_grep '.escapeshellarg($file_path).' '.escapeshellarg($GLOBALS['BEGIN']).' '.escapeshellarg($GLOBALS['END']));
}

function read_last_line($file_path)
{
    $output = [];
    $ret = 0;
    exec('tac '.escapeshellarg($file_path)." | grep -m 1 '[^[:blank:]]'", $output, $ret);

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

/*
 * What counts as a password.
 *
 * The rule and the sentence shown next to it did not describe the same thing. It read
 *
 *     ^\S*(?=\S{8,31})(?=\S*[a-z])(?=\S*[A-Z])(?=\S*[\d])\S*$
 *
 * where \S is "anything that is not whitespace", so a space anywhere refused the password -
 * silently, since the message said nothing about spaces and talked about a 32 character
 * limit instead. There was no such limit: the lookahead only has to find eight characters,
 * so the 31 never bound anything and a password of any length was accepted. Being told to
 * use a name and a birthday and then having it rejected with a sentence about length is how
 * that reads from the outside, and it is why a hyphen got the blame - a hyphen was always
 * fine.
 *
 * So: spaces are allowed inside, because a passphrase is a good password and the people who
 * recommend not banning characters are right. Not at either end, because whitespace that
 * cannot be seen is a password nobody can retype. Seventy two is where bcrypt stops reading,
 * so anything past it would be accepted here and then quietly ignored - a real limit, stated
 * rather than discovered.
 *
 * Counted in bytes, like bcrypt counts, so an accented character costing two of them is
 * measured the way the thing storing it measures.
 */
function validatePassword($pwd)
{
    if (!is_string($pwd) || strlen($pwd) < 8 || strlen($pwd) > 72) {
        return false;
    }

    if ($pwd !== trim($pwd)) {
        return false;
    }

    // no \s inside the character tests: a space is an ordinary character here
    return 1 === preg_match('/^(?=.*[a-z])(?=.*[A-Z])(?=.*\d).+$/sD', $pwd);
}

function compareCaptcha($v1,$v2){
   return strtolower($v1) === strtolower($v2);
}

?>
