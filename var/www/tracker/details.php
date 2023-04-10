<?php

//ini_set('display_startup_errors', 1);
//error_reporting(E_ALL);

require 'lib.php';
require 'database.php';

session_start();

validateSession();
if (isReadOnly()) {
    exit();
}

$user = findUser($_SESSION['login']);

echo $user[1].','.$user[2].','.$user[3].','.$user[4];
?>