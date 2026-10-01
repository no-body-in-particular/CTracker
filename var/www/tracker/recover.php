<?php

include_once 'database.php';
include_once 'lib.php';

start_session();

$message = '';

if ('POST' === $_SERVER['REQUEST_METHOD'] ) {
   // Checking that the posted phrase match the phrase stored in the session

   if (isset($_SESSION['phrase']) && compareCaptcha($_SESSION['phrase'], $_POST['phrase'])) {
      // checked before the address is handed to mail(), which is the one place a newline in
      // it would become a header of the caller's choosing. getResetKey() only answers for an
      // address already on file, so this was never reachable from outside - but "not
      // reachable today" is a property of the caller, not of this code.
      $email = validateEmail($_POST['email'] ?? '') ? $_POST['email'] : '';
      $key = $email ? getResetKey($email) : null;

      // said whether or not an account was found, and whether or not the mail got out. The
      // first is deliberate - a different answer here tells a stranger which addresses are
      // registered. The second is not something this page can know: the message is handed to
      // the queue, and delivery happens later.
      $message='Mail sent. Please check your mailbox for a password reset link.';
         if($key){
            // Wrap the sentence, never the URL: wordwrap() with cut=true will
            // break a long link mid-token, and a base64 key with enough
            // +, / or = in it pushes the URL past 70 characters (76 at worst).
            // That silently mailed an unclickable link about 1 time in 500.
            $link = "https://coredump.ws/tracker/reset.php?key=" . urlencode($key);
            $msg = wordwrap("You can reset your password with the following link:", 70, "\n", true)
                 . "\n" . $link;

            /*
             * It used to go out with no headers at all, so both the From and the envelope
             * sender were whatever user the web server runs as - hiawatha@coredump.ws, which
             * is a system account and not a mailbox. Mail from an address that cannot be
             * replied to and cannot take a bounce is exactly the shape receivers score
             * against, and a bounce had nowhere to go, so a failure was invisible at both
             * ends.
             *
             * The -f sets the envelope sender, which is the address SPF is checked against -
             * not the From header - so it has to be a real one on this domain.
             */
            $from = 'admin@coredump.ws';
            $headers = implode("\r\n", [
                'From: GPS tracker <'.$from.'>',
                'Reply-To: '.$from,
                'Content-Type: text/plain; charset=UTF-8',
                //so an out of office reply does not answer a robot
                'Auto-Submitted: auto-generated',
            ]);

            mail($email, "GPS tracker password reset", $msg, $headers, '-f'.$from);
         }
      }else{
       //  $message= $_SESSION['phrase'] . '  ' . $_POST['phrase'];
         $message='Please enter the Captcha correctly.';
      }
}
?>
<html>
   <head>
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <link rel="stylesheet" href="style/index.css" type="text/css">
      <script src="jquery/jquery-3.2.1.min.js"></script>
      <script src="js/store.js"></script>
   </head>
   <body>
      <div class="login-page">
         <div class="form">
            <form method="post" class="login-form" action="<?php echo htmlspecialchars($_SERVER['PHP_SELF'], ENT_QUOTES, 'UTF-8'); ?>" >
                <input style="width:100%" type="text" placeholder="email address" class="input" name="email" id="email"/>
                <img src="captcha.php" /><br>
                <input type="text" name="phrase" class="input" placeholder="Captcha" /><br>
                <button style="width:100%" class="button">reset password</button>
                <p class="message"><?php echo $message; ?></p>
            </form>
         </div>
      </div>
   </body>
</html>
