<?php

include 'database.php';

ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);


$message = '<a href="registration.php">register here</a><br><br><a href="recover.php">forgot password?</a>';
session_start();
session_unset();

if (isset($_GET['login'])) {
    if (validateUser($_POST['username'], hash('whirlpool', $_POST['password']))) {
        header('Location: tracker.php');
    } else {
        $message = '<a>Wrong Username or Password</a>';
    }
}
?>
<html>
   <head>
      <link rel="stylesheet" href="style/index.css" type="text/css">
      <script src="jquery/jquery-3.2.1.min.js"></script>
      <script src="js/store.js"></script>
   </head>
   <body>
      <div class="login-page">
         <div class="form">
            <form method="post" class="login-form" action="<?php echo $_SERVER['PHP_SELF']; ?>?login=true" >
                <input style="width:100%" type="text" placeholder="username" class="input" name="username" id="username"/>
                <input style="width:100%" type="password" placeholder="password" class="input" name="password" id="password"/>
               <button style="width:100%" class="button">login</button>
                <p class="message"><?php echo $message; ?></p>
            </form>
         </div>
      </div>
   </body>
</html>
