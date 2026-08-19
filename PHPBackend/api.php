<?php

$config = require __DIR__ . '/config.php';

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, X-Device-Key');
header('Cache-Control: no-store');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

function json_response(int $status, array $data): void
{
    http_response_code($status);
    header('Content-Type: application/json');
    echo json_encode($data);
    exit;
}

function get_device_key(): string
{
    $header = $_SERVER['HTTP_X_DEVICE_KEY'] ?? '';
    if ($header !== '') {
        return $header;
    }
    return $_GET['key'] ?? '';
}

function require_device_key(array $config): void
{
    if (!hash_equals($config['device_key'], get_device_key())) {
        json_response(401, ['ok' => false, 'error' => 'bad device key']);
    }
}

function read_body(): string
{
    if (isset($_POST['frame_hex']) && is_string($_POST['frame_hex']) && $_POST['frame_hex'] !== '') {
        return json_encode([
            'width' => (int)($_POST['width'] ?? 64),
            'height' => (int)($_POST['height'] ?? 64),
            'frameHex' => $_POST['frame_hex'],
        ], JSON_UNESCAPED_SLASHES);
    }

    if (isset($_POST['payload_b64']) && is_string($_POST['payload_b64']) && $_POST['payload_b64'] !== '') {
        $decoded = base64_decode($_POST['payload_b64'], true);
        if ($decoded === false || $decoded === '') {
            json_response(400, ['ok' => false, 'error' => 'invalid base64 payload']);
        }
        if (strlen($decoded) > 160000) {
            json_response(413, ['ok' => false, 'error' => 'payload too large']);
        }
        return $decoded;
    }

    if (isset($_POST['payload']) && is_string($_POST['payload']) && $_POST['payload'] !== '') {
        return $_POST['payload'];
    }

    $body = file_get_contents('php://input');
    if ($body === false || $body === '') {
        json_response(400, ['ok' => false, 'error' => 'empty body']);
    }
    if (strlen($body) > 160000) {
        json_response(413, ['ok' => false, 'error' => 'payload too large']);
    }
    return $body;
}

function validate_matrix_payload(array $payload): void
{
    if (($payload['width'] ?? null) !== 64 || ($payload['height'] ?? null) !== 64) {
        json_response(400, ['ok' => false, 'error' => 'width and height must be 64']);
    }

    if (isset($payload['frameHex'])) {
        if (!is_string($payload['frameHex']) || strlen($payload['frameHex']) !== 64 * 64 * 6 || !preg_match('/\A[0-9a-fA-F]+\z/', $payload['frameHex'])) {
            json_response(400, ['ok' => false, 'error' => 'frameHex must contain 4096 RGB hex pixels']);
        }
        $payload['frameHex'] = strtolower($payload['frameHex']);
        return;
    }

    if (!isset($payload['pixels']) || !is_array($payload['pixels']) || count($payload['pixels']) !== 4096) {
        json_response(400, ['ok' => false, 'error' => 'pixels must contain 4096 RGB objects']);
    }

    foreach ($payload['pixels'] as $pixel) {
        foreach (['r', 'g', 'b'] as $channel) {
            if (!isset($pixel[$channel]) || !is_int($pixel[$channel]) || $pixel[$channel] < 0 || $pixel[$channel] > 255) {
                json_response(400, ['ok' => false, 'error' => 'pixels must use integer r/g/b values from 0 to 255']);
            }
        }
    }
}

$action = $_GET['action'] ?? 'latest';

if ($_SERVER['REQUEST_METHOD'] === 'GET' && $action === 'health') {
    $hasFrame = is_file($config['storage_file']);
    json_response(200, ['ok' => true, 'hasFrame' => $hasFrame]);
}

if ($_SERVER['REQUEST_METHOD'] === 'GET' && $action === 'latest') {
    require_device_key($config);
    if (!is_file($config['storage_file'])) {
        json_response(200, ['ok' => true, 'hasFrame' => false]);
    }

    header('Content-Type: application/json');
    readfile($config['storage_file']);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && $action === 'matrix') {
    require_device_key($config);
    $body = read_body();
    $payload = json_decode($body, true);
    if (!is_array($payload)) {
        json_response(400, ['ok' => false, 'error' => 'invalid json']);
    }

    validate_matrix_payload($payload);
    if (isset($payload['frameHex'])) {
        $payload['frameHex'] = strtolower($payload['frameHex']);
    }
    $payload['updatedAt'] = gmdate('c');
    $payload['sequence'] = time();

    $encoded = json_encode($payload, JSON_UNESCAPED_SLASHES);
    if ($encoded === false || file_put_contents($config['storage_file'], $encoded, LOCK_EX) === false) {
        json_response(500, ['ok' => false, 'error' => 'could not save frame']);
    }

    json_response(200, ['ok' => true, 'pixels' => 4096, 'sequence' => $payload['sequence']]);
}

json_response(404, ['ok' => false, 'error' => 'not found']);
