<?php

 function drawLine($image, $width, $height, $tcol = null): void
 {
     $red = random_int(100, 255);
     $green = random_int(100, 255);
     $blue = random_int(100, 255);

     if (random_int(0, 1)) { // Horizontal
         $Xa = random_int(0, $width / 2);
         $Ya = random_int(0, $height);
         $Xb = random_int($width / 2, $width);
         $Yb = random_int(0, $height);
     } else { // Vertical
         $Xa = random_int(0, $width);
         $Ya = random_int(0, $height / 2);
         $Xb = random_int(0, $width);
         $Yb = random_int($height / 2, $height);
     }

     imagesetthickness($image, random_int(2, 6));
     imageline($image, $Xa, $Ya, $Xb, $Yb, imagecolorallocate($image, $red, $green, $blue));
 }


 function getRGB($col)
 {
     return [
         (int) ($col >> 16) & 0xff,
         (int) ($col >> 8) & 0xff,
         (int) ($col) & 0xff,
     ];
 }

 function interpolate($x, $y, $nw, $ne, $sw, $se)
 {
     list($r0, $g0, $b0) = getRGB($nw);
     list($r1, $g1, $b1) = getRGB($ne);
     list($r2, $g2, $b2) = getRGB($sw);
     list($r3, $g3, $b3) = getRGB($se);

     $cx = 1.0 - $x;
     $cy = 1.0 - $y;

     $m0 = $cx * $r0 + $x * $r1;
     $m1 = $cx * $r2 + $x * $r3;
     $r = (int) ($cy * $m0 + $y * $m1);

     $m0 = $cx * $g0 + $x * $g1;
     $m1 = $cx * $g2 + $x * $g3;
     $g = (int) ($cy * $m0 + $y * $m1);

     $m0 = $cx * $b0 + $x * $b1;
     $m1 = $cx * $b2 + $x * $b3;
     $b = (int) ($cy * $m0 + $y * $m1);

     return ($r << 16) | ($g << 8) | $b;
 }

 function getCol($image, $x, $y, $background)
 {
     $L = imagesx($image);
     $H = imagesy($image);
     if ($x < 0 || $x >= $L || $y < 0 || $y >= $H) {
         return $background;
     }

     return imagecolorat($image, $x, $y);
 }

 function build($width = 300, $height = 80, $phrase='test')
 {
     $font = __DIR__.'/Font/'.random_int(0, 5).'.ttf';

     // create image and fill background colour
     $image = imagecreatetruecolor($width, $height);
     imagefill($image, 0, 0, imagecolorallocate($image, random_int(200, 255), random_int(200, 255), random_int(200, 255)));

     //draw lines behidn the text
     $line_count = random_int(4, 15);
     for ($e = 0; $e < $line_count; ++$e) {
         drawLine($image, $width, $height);
     }

     // Write CAPTCHA text
     $color =  $length = mb_strlen($phrase);

     // Gets the text size and start position
     $size = $width / $length - random_int(0, 3) - 1;
     $box = imagettfbbox($size, 0, $font, $phrase);
     $textWidth = $box[2] - $box[0];
     $textHeight = $box[1] - $box[7];
     $x = ($width - $textWidth) / 2;
     $y = ($height - $textHeight) / 2 + $size;

     $textColor = [random_int(0, 200), random_int(0, 200), random_int(0, 200)];
     $color = imagecolorallocate($image, $textColor[0], $textColor[1], $textColor[2]);

     //write each letter at a semi-random position
     for ($i = 0; $i < $length; ++$i) {
         $symbol = mb_substr($phrase, $i, 1);
         $box = imagettfbbox($size, 0, $font, $symbol);
         $w = $box[2] - $box[0];
         $angle = random_int(-10, 10);//max angle
         $yoffset = random_int(-3, 3);//max offset
         $xoffset = random_int(-4, 4);//max offset

         imagettftext($image, $size, $angle, $x +$xoffset, $y + $yoffset, $color, $font, $symbol);
         $x += $w;
     }

     // draw lines in front of the text
     $line_count = random_int(2, 6);
     for ($e = 0; $e < $line_count; ++$e) {
         drawLine($image, $width, $height, $color);
     }

     $contents = imagecreatetruecolor($width, $height);
     $X = random_int(0, $width);
     $Y = random_int(0, $height);
     $phase = random_int(0, 10);
     $scale = 1.1 + random_int(0, 10000) / 30000;
     for ($x = 0; $x < $width; ++$x) {
         for ($y = 0; $y < $height; ++$y) {
             $Vx = $x - $X;
             $Vy = $y - $Y;
             $Vn = sqrt($Vx * $Vx + $Vy * $Vy);

             if (0 != $Vn) {
                 $Vn2 = $Vn + 4 * sin($Vn / 30);
                 $nX = $X + ($Vx * $Vn2 / $Vn);
                 $nY = $Y + ($Vy * $Vn2 / $Vn);
             } else {
                 $nX = $X;
                 $nY = $Y;
             }
             $nY = $nY + $scale * sin($phase + $nX * 0.2);

             $p = interpolate(
                 $nX - floor($nX),
                 $nY - floor($nY),
                 getCol($image, floor($nX), floor($nY), $bg),
                 getCol($image, ceil($nX), floor($nY), $bg),
                 getCol($image, floor($nX), ceil($nY), $bg),
                 getCol($image, ceil($nX), ceil($nY), $bg)
             );


             if (0 == $p) {
                 $p = $bg;
             }

             imagesetpixel($contents, $x, $y, $p);
         }
     }

     if (0 === random_int(0, 1)) {
         imagefilter($contents, IMG_FILTER_NEGATE);
     }

     if (0 === random_int(0, 10)) {
         imagefilter($contents, IMG_FILTER_EDGEDETECT);
     }

     imagefilter($contents, IMG_FILTER_CONTRAST, random_int(-50, 10));

     if (0 === random_int(0, 5)) {
         imagefilter($contents, IMG_FILTER_COLORIZE, random_int(-80, 50), random_int(-80, 50), random_int(-80, 50));
     }

     return $contents;
 }


 session_start();

 $phrase='';
 $chars = str_split('abcdefghijklmnpqrstuvwxyz123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ');
 $length = random_int(5, 10);

 for ($i = 0; $i <  $length; ++$i) {
     $phrase .= $chars[array_rand($chars)];
 }

 $_SESSION['phrase'] = $phrase;

 header('Content-Type: image/jpeg');
 $jpeg=build(500,140,$phrase);
 imagejpeg($jpeg, null, 90);

?>