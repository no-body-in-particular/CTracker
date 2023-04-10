<?php

require 'lib.php';
require 'database.php';

session_start();
validateSession();

if (isReadonly()) {
    exit();
}

if (!validateEmail($_GET['email'])) {
    exit('Invalid email address');
}

if (!validateName($_GET['name'])) {
    exit('Invalid name');
}
if (!validateUsername($_GET['username'])) {
    exit('Invalid username');
}

$loggedinUser=findUser($_SESSION['login']);

if ($_SESSION['login'] !== $_GET['username'] && findUser($_GET['username'])) {
    exit('New username already exists');
}

if ($loggedinUser[3] != $_GET['email'] && findUserByEmail($_GET['email'])) {
    exit('An user with this email already exists.');
}

$ret = updateUser($_SESSION['uid'], $_GET['username'], $_GET['name'], $_GET['email'], $_GET['pwd']);

if ($ret) {
    $message = 'Success';
} else {
    $message = 'Update failed';
}

?>