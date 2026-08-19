# Legacy InfinityFree PHP Backend

The active Draw Anywhere setup now uses Firebase Realtime Database. Keep your real Firebase URL in ignored local config files.

```text
https://your-database.firebaseio.com/matrix/latest.json
```

Keep this PHP backend only as an optional fallback for InfinityFree hosting.

Upload every file in this folder to an InfinityFree directory such as:

```text
htdocs/matrix/
```

Then edit `config.php` on the host and change:

```php
'device_key' => 'b54f08623ae4039f55bcecba4961037fb4513d2ba9cb2b0667c5db970ac94911',
```

Use a long private value. Put the same value in the frontends and ESP32 firmware.

The backend writes the latest frame to `matrix_latest.json`. The included
`.htaccess` blocks direct browser access to that file, so clients should only use
`api.php`.

## URLs

If your site is:

```text
https://cory-pearl.gt.tc/matrix/
```

Use this in the HTML or Swift app:

```text
https://cory-pearl.gt.tc/matrix/api.php?action=matrix&key=b54f08623ae4039f55bcecba4961037fb4513d2ba9cb2b0667c5db970ac94911
```

Use this in the ESP32 firmware backend URL:

```text
https://cory-pearl.gt.tc/matrix/api.php?action=latest&key=b54f08623ae4039f55bcecba4961037fb4513d2ba9cb2b0667c5db970ac94911
```

## Why Polling

InfinityFree cannot normally push directly to an ESP32 on a private network. The frontend posts the latest 64x64 frame to PHP, and the ESP32 polls PHP to pull the newest frame.
