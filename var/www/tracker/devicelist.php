<?php
include 'database.php';
include 'lib.php';
include 'config.php';

//ini_set('display_errors', 1);
//ini_set('display_startup_errors', 1);
//error_reporting(E_ALL);

validateSession();

$ACTION = $_GET['action'];

switch ($ACTION) {
    case 'write':
        if (isReadonly()) {
            exit();
        }

        if (deviceClaimed($_GET['imei'])) {
            exit('This device belongs to another user');
        }

        if (!file_exists(DEVPATH.$_GET['imei'].'.log.txt')) {
            exit('This device has not connected to our server yet');
        }

        if (!validateName($_GET['name'])) {
            exit('Invalid name');
        }

        addDevice($_GET['imei'], $_GET['name']);

        break;

    case 'remove':
        if (isReadonly()) {
            exit();
        }

        if (!removeDevice($_GET['imei'])) {
            exit('failed to remove device.');
        }
        //ask are you sure
        //remove device
        break;

    case 'read':
    default:
        readDevices();

        break;
}

?>

