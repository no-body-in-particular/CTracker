<?php
include 'database.php';
include 'lib.php';


session_start();

$message = '';
$key=$_GET['key'];
$url_key=urlencode($key);
$pwd=$_POST['password'];

$user=getResetUser($key);

if(!$user){
   exit('<html<body>Invalid or expired link.<meta http-equiv="Refresh" content="3; url=index.php" /></body></html>');
}

if ('POST' === $_SERVER['REQUEST_METHOD'] ) {
   if(validatePassword($pwd)){
      updatePassword($user[0],$pwd);
      $message='Password changed. <meta http-equiv="Refresh" content="4; url=index.php"></meta>';
   }else{
      $message='Invalid password. Must be between 8 and 32 characters containing at least 1 uppercase, 1 lowercase letter and 1 number.';
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
            <form method="post" class="reset-form action="<?php echo $_SERVER['PHP_SELF']; ?>?key=$url_key" >
                <input style="width:100%" type="password" placeholder="new password" class="input" name="password" id="password"/>
                <input type="submit" value ="Set password" />
                <p class="message"><?php echo $message; ?></p>
            </form>
         </div>
      </div>
   </body>
</html>
