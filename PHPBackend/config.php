<?php

return [
    // Change this before uploading. Use the same value in the ESP32 firmware menuconfig.
    'device_key' => 'b54f08623ae4039f55bcecba4961037fb4513d2ba9cb2b0667c5db970ac94911',

    // InfinityFree allows normal PHP file writes inside your hosting account.
    // Keep this path outside public links when your host allows it.
    'storage_file' => __DIR__ . '/matrix_latest.json',
];
