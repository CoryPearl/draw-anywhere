# Firebase Setup

Draw Anywhere now uses Firebase Realtime Database as the shared backend.

Your database URL should look like this:

```text
https://your-database.firebaseio.com/matrix/latest.json
```

The web app and iOS app write the newest frame with `PUT`. The ESP32 reads the same URL with `GET`.

## Test Rules

For first hardware testing, open Firebase Console, go to Realtime Database, then Rules, and use:

```json
{
  "rules": {
    "matrix": {
      ".read": true,
      ".write": true
    }
  }
}
```

These rules make the matrix endpoint public. That is fine for testing, but not private.

## Expected Data

After pressing Send, your private Firebase URL should show JSON instead of `Permission denied`.

The JSON should include:

```json
{
  "ok": true,
  "hasFrame": true,
  "width": 64,
  "height": 64,
  "frameHex": "rrggbb...",
  "sequence": 123
}
```

If the ESP32 logs `Backend returned invalid matrix payload` and the response starts with `{"error":"Permission denied"}`, the Firebase rules are still blocking it.
