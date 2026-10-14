<?php
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: *");
header("Access-Control-Allow-Methods: *");

$ip  = $_GET["ip"] ?? "";
$url = $_GET["url"] ?? "";

if (!$ip || !$url) {
    echo "Missing parameters";
    exit;
}

$target = "http://$ip$url";
echo file_get_contents($target);
