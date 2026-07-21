"""
BeatMate V11 - License Server
Ultra-secure backend for license generation, validation, and payment processing.

Run in development:
    flask --app server run --debug --port 5000

Run in production:
    gunicorn -w 4 -b 0.0.0.0:8000 server:app
"""

import hashlib
import hmac
import logging
import os
import random
import re
import secrets
import sqlite3
import string
import time
from datetime import datetime, timedelta, timezone
from functools import wraps
from logging.handlers import RotatingFileHandler

import bleach
import stripe
from dotenv import load_dotenv
from flask import Flask, abort, g, jsonify, request
from flask_cors import CORS
from flask_limiter import Limiter
from flask_limiter.util import get_remote_address

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
load_dotenv()

app = Flask(__name__)
app.config["SECRET_KEY"] = os.getenv("SECRET_KEY", secrets.token_hex(64))
app.config["DATABASE_PATH"] = os.getenv("DATABASE_PATH", "licenses.db")

IS_DEV = os.getenv("FLASK_ENV", "production").lower() == "development"

# Stripe
stripe.api_key = os.getenv("STRIPE_SECRET_KEY", "")
STRIPE_WEBHOOK_SECRET = os.getenv("STRIPE_WEBHOOK_SECRET", "")

# PayPal
PAYPAL_CLIENT_ID = os.getenv("PAYPAL_CLIENT_ID", "")
PAYPAL_CLIENT_SECRET = os.getenv("PAYPAL_CLIENT_SECRET", "")
PAYPAL_MODE = os.getenv("PAYPAL_MODE", "sandbox")
PAYPAL_API_BASE = (
    "https://api-m.paypal.com"
    if PAYPAL_MODE == "live"
    else "https://api-m.sandbox.paypal.com"
)

# SMTP
SMTP_HOST = os.getenv("SMTP_HOST", "")
SMTP_PORT = int(os.getenv("SMTP_PORT", "587"))
SMTP_USER = os.getenv("SMTP_USER", "")
SMTP_PASSWORD = os.getenv("SMTP_PASSWORD", "")

# CORS - strict whitelist
CORS_ORIGINS = [
    o.strip()
    for o in os.getenv("CORS_ORIGINS", "https://beatmate.app").split(",")
    if o.strip()
]

CORS(
    app,
    resources={r"/api/*": {"origins": CORS_ORIGINS}},
    supports_credentials=False,
    methods=["GET", "POST"],
    allow_headers=["Content-Type", "Authorization"],
)

# Rate limiter
limiter = Limiter(
    key_func=get_remote_address,
    app=app,
    default_limits=["60 per minute"],
    storage_uri=os.getenv("RATELIMIT_STORAGE_URI", "memory://"),
)

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
os.makedirs(LOG_DIR, exist_ok=True)

file_handler = RotatingFileHandler(
    os.path.join(LOG_DIR, "server.log"),
    maxBytes=10 * 1024 * 1024,  # 10 MB
    backupCount=5,
)
file_handler.setFormatter(
    logging.Formatter(
        "[%(asctime)s] %(levelname)s %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
)
file_handler.setLevel(logging.INFO)
app.logger.addHandler(file_handler)
app.logger.setLevel(logging.INFO)

logger = app.logger

# ---------------------------------------------------------------------------
# Security headers (Helmet-style)
# ---------------------------------------------------------------------------
@app.after_request
def set_security_headers(response):
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["X-XSS-Protection"] = "1; mode=block"
    response.headers["Referrer-Policy"] = "strict-origin-when-cross-origin"
    response.headers["Content-Security-Policy"] = (
        "default-src 'none'; frame-ancestors 'none'"
    )
    response.headers["Strict-Transport-Security"] = (
        "max-age=63072000; includeSubDomains; preload"
    )
    response.headers["Permissions-Policy"] = (
        "geolocation=(), microphone=(), camera=()"
    )
    # Remove server identification
    response.headers.pop("Server", None)
    return response


# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------
DB_SCHEMA = """
CREATE TABLE IF NOT EXISTS licenses (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    license_key     TEXT    UNIQUE NOT NULL,
    tier            TEXT    NOT NULL CHECK(tier IN ('E','P','S')),
    billing         TEXT    NOT NULL CHECK(billing IN ('A','M')),
    email           TEXT    NOT NULL,
    nom             TEXT    NOT NULL DEFAULT '',
    prenom          TEXT    NOT NULL DEFAULT '',
    hwid            TEXT    DEFAULT NULL,
    mac_address     TEXT    DEFAULT NULL,
    activated       INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    expires_at      TEXT    DEFAULT NULL,
    payment_provider TEXT   DEFAULT NULL,
    payment_id      TEXT    DEFAULT NULL,
    is_test         INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS validation_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    license_key     TEXT    NOT NULL,
    hwid            TEXT    DEFAULT NULL,
    ip_address      TEXT    DEFAULT NULL,
    user_agent      TEXT    DEFAULT NULL,
    result          TEXT    NOT NULL,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS transaction_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type      TEXT    NOT NULL,
    provider        TEXT    NOT NULL,
    payment_id      TEXT    DEFAULT NULL,
    email           TEXT    DEFAULT NULL,
    ip_address      TEXT    DEFAULT NULL,
    user_agent      TEXT    DEFAULT NULL,
    details         TEXT    DEFAULT NULL,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_licenses_key ON licenses(license_key);
CREATE INDEX IF NOT EXISTS idx_licenses_email ON licenses(email);
CREATE INDEX IF NOT EXISTS idx_licenses_hwid ON licenses(hwid);
"""


def get_db() -> sqlite3.Connection:
    """Return a per-request database connection."""
    if "db" not in g:
        db_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            app.config["DATABASE_PATH"],
        )
        g.db = sqlite3.connect(db_path)
        g.db.row_factory = sqlite3.Row
        g.db.execute("PRAGMA journal_mode=WAL")
        g.db.execute("PRAGMA foreign_keys=ON")
    return g.db


@app.teardown_appcontext
def close_db(exception):
    db = g.pop("db", None)
    if db is not None:
        db.close()


def init_db():
    """Initialize the database schema."""
    db_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        app.config["DATABASE_PATH"],
    )
    conn = sqlite3.connect(db_path)
    conn.executescript(DB_SCHEMA)
    conn.close()
    logger.info("Database initialized at %s", db_path)


# Initialize on import
with app.app_context():
    init_db()


# ---------------------------------------------------------------------------
# Input validation helpers
# ---------------------------------------------------------------------------
EMAIL_RE = re.compile(
    r"^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@"
    r"[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?"
    r"(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)+$"
)

LICENSE_KEY_RE = re.compile(r"^[A-Z0-9]{5}-[A-Z0-9]{5}-[A-Z0-9]{5}-[A-Z0-9]{5}$")

HWID_RE = re.compile(r"^[a-fA-F0-9\-]{8,128}$")

MAC_RE = re.compile(r"^([0-9A-Fa-f]{2}[:\-]){5}[0-9A-Fa-f]{2}$")

VALID_PLANS = {"essential", "pro", "studio"}
PLAN_TIER_MAP = {"essential": "E", "pro": "P", "studio": "S"}
VALID_BILLING = {"monthly", "annual"}
BILLING_CODE_MAP = {"monthly": "M", "annual": "A"}


def sanitize(text: str, max_len: int = 200) -> str:
    """Sanitize text input: strip HTML, limit length."""
    if not isinstance(text, str):
        return ""
    cleaned = bleach.clean(text, tags=[], attributes={}, strip=True)
    return cleaned[:max_len].strip()


def validate_email(email: str) -> bool:
    return bool(EMAIL_RE.match(email)) and len(email) <= 254


def validate_license_key(key: str) -> bool:
    return bool(LICENSE_KEY_RE.match(key))


def validate_hwid(hwid: str) -> bool:
    return bool(HWID_RE.match(hwid))


def validate_mac(mac: str) -> bool:
    return bool(MAC_RE.match(mac))


def require_json(*fields):
    """Decorator that validates required JSON fields are present."""

    def decorator(f):
        @wraps(f)
        def wrapper(*args, **kwargs):
            data = request.get_json(silent=True)
            if data is None:
                return jsonify({"error": "Invalid JSON body"}), 400
            missing = [field for field in fields if field not in data]
            if missing:
                return (
                    jsonify(
                        {"error": f"Missing required fields: {', '.join(missing)}"}
                    ),
                    400,
                )
            return f(*args, **kwargs)

        return wrapper

    return decorator


def log_transaction(event_type: str, provider: str, **kwargs):
    """Log a transaction to the database."""
    db = get_db()
    db.execute(
        """INSERT INTO transaction_log
           (event_type, provider, payment_id, email, ip_address, user_agent, details)
           VALUES (?, ?, ?, ?, ?, ?, ?)""",
        (
            event_type,
            provider,
            kwargs.get("payment_id"),
            kwargs.get("email"),
            request.remote_addr,
            request.headers.get("User-Agent", "")[:500],
            kwargs.get("details"),
        ),
    )
    db.commit()


# ---------------------------------------------------------------------------
# Luhn algorithm - exact port of LicenseValidator.cpp
# ---------------------------------------------------------------------------
def luhn_check(s: str) -> bool:
    """
    Luhn check on an alphanumeric string.
    Exact port of BeatMate::Services::Security::LicenseValidator::luhnCheck.
    """
    total = 0
    alternate = False
    for c in reversed(s):
        if c.isdigit():
            n = ord(c) - ord("0")
        elif c.isalpha():
            n = ord(c.upper()) - ord("A") + 10
        else:
            continue
        if alternate:
            n *= 2
            if n > 9:
                n -= 9
        total += n
        alternate = not alternate
    return total % 10 == 0


def validate_format(key: str) -> bool:
    """Validate license key format: XXXXX-XXXXX-XXXXX-XXXXX (23 chars)."""
    if len(key) != 23:
        return False
    for i, c in enumerate(key):
        if i in (5, 11, 17):
            if c != "-":
                return False
        elif not c.isalnum():
            return False
    return True


def generate_key(tier: str = "E", billing: str = "A") -> str:
    """
    Generate a license key in format XXXXX-XXXXX-XXXXX-XXXXX that passes Luhn.

    First char encodes tier (E=Essential, P=Pro, S=Studio).
    Second char encodes billing (A=Annual, M=Monthly).
    Remaining 18 chars are random alphanumeric (uppercase + digits).
    Last char is adjusted so the full alphanumeric string passes luhn_check.
    """
    charset = string.ascii_uppercase + string.digits  # A-Z, 0-9

    # First two chars encode plan metadata
    prefix = tier.upper() + billing.upper()

    # Generate 17 random chars (total alphanumeric = 2 prefix + 17 random + 1 checksum = 20)
    body = "".join(secrets.choice(charset) for _ in range(17))

    # We need to find a check character so that the full 20-char string passes Luhn.
    base = prefix + body  # 19 chars

    for candidate in charset:
        full = base + candidate
        if luhn_check(full):
            # Format as XXXXX-XXXXX-XXXXX-XXXXX
            return f"{full[0:5]}-{full[5:10]}-{full[10:15]}-{full[15:20]}"

    # Fallback: should never happen with 36 candidates, but be safe
    # Try shifting body and retry
    body = "".join(secrets.choice(charset) for _ in range(17))
    base = prefix + body
    for candidate in charset:
        full = base + candidate
        if luhn_check(full):
            return f"{full[0:5]}-{full[5:10]}-{full[10:15]}-{full[15:20]}"

    raise RuntimeError("Failed to generate a valid Luhn key")


def compute_expiry(billing: str) -> str:
    """Compute license expiry date based on billing cycle."""
    now = datetime.now(timezone.utc)
    if billing == "A":
        expires = now + timedelta(days=365)
    else:
        expires = now + timedelta(days=30)
    return expires.strftime("%Y-%m-%d %H:%M:%S")


# ---------------------------------------------------------------------------
# Email sending
# ---------------------------------------------------------------------------
def send_license_email(email: str, nom: str, prenom: str, key: str, tier: str, expires_at: str):
    """Send the license key to the customer via email."""
    if not SMTP_HOST or not SMTP_USER:
        logger.warning("SMTP not configured, skipping email to %s", email)
        return False

    import smtplib
    from email.mime.multipart import MIMEMultipart
    from email.mime.text import MIMEText

    tier_names = {"E": "Essential", "P": "Pro", "S": "Studio"}
    tier_name = tier_names.get(tier, "Essential")

    subject = f"BeatMate V11 - Your {tier_name} License Key"
    body_html = f"""
    <html>
    <body style="font-family: Arial, sans-serif; color: #333; max-width: 600px; margin: 0 auto;">
        <div style="background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
                    padding: 30px; text-align: center; border-radius: 8px 8px 0 0;">
            <h1 style="color: #e94560; margin: 0;">BeatMate V11</h1>
            <p style="color: #ccc; margin-top: 8px;">{tier_name} License</p>
        </div>
        <div style="padding: 30px; background: #f8f9fa; border-radius: 0 0 8px 8px;">
            <p>Bonjour {bleach.clean(prenom)} {bleach.clean(nom)},</p>
            <p>Merci pour votre achat ! Voici votre cl&eacute; de licence :</p>
            <div style="background: #1a1a2e; color: #e94560; font-family: 'Courier New', monospace;
                        font-size: 20px; padding: 15px; text-align: center; border-radius: 6px;
                        letter-spacing: 2px; margin: 20px 0;">
                {bleach.clean(key)}
            </div>
            <p><strong>Plan :</strong> {tier_name}</p>
            <p><strong>Expire le :</strong> {bleach.clean(expires_at)}</p>
            <hr style="border: 1px solid #ddd; margin: 20px 0;">
            <p style="font-size: 12px; color: #888;">
                Cette cl&eacute; est personnelle et li&eacute;e &agrave; votre machine.
                Ne la partagez pas.
            </p>
        </div>
    </body>
    </html>
    """

    msg = MIMEMultipart("alternative")
    msg["Subject"] = subject
    msg["From"] = SMTP_USER
    msg["To"] = email
    msg.attach(MIMEText(body_html, "html"))

    try:
        with smtplib.SMTP(SMTP_HOST, SMTP_PORT, timeout=10) as server:
            server.starttls()
            server.login(SMTP_USER, SMTP_PASSWORD)
            server.sendmail(SMTP_USER, email, msg.as_string())
        logger.info("License email sent to %s", email)
        return True
    except Exception:
        logger.exception("Failed to send email to %s", email)
        return False


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

# --- Health check ---
@app.route("/api/health", methods=["GET"])
@limiter.limit("10 per minute")
def health():
    return jsonify({"status": "ok", "version": "1.0.0", "timestamp": datetime.now(timezone.utc).isoformat()})


# --- Create checkout ---
@app.route("/api/create-checkout", methods=["POST"])
@limiter.limit("10 per minute")
@require_json("plan", "billing", "nom", "prenom", "email")
def create_checkout():
    data = request.get_json()

    # Validate and sanitize
    plan = sanitize(data["plan"]).lower()
    billing = sanitize(data["billing"]).lower()
    nom = sanitize(data["nom"], max_len=100)
    prenom = sanitize(data["prenom"], max_len=100)
    email = sanitize(data["email"], max_len=254)

    if plan not in VALID_PLANS:
        return jsonify({"error": f"Invalid plan. Must be one of: {', '.join(VALID_PLANS)}"}), 400

    if billing not in VALID_BILLING:
        return jsonify({"error": "Invalid billing. Must be 'monthly' or 'annual'"}), 400

    if not validate_email(email):
        return jsonify({"error": "Invalid email address"}), 400

    if not nom or not prenom:
        return jsonify({"error": "Name fields cannot be empty"}), 400

    tier = PLAN_TIER_MAP[plan]
    billing_code = BILLING_CODE_MAP[billing]

    log_transaction(
        "checkout_initiated",
        "system",
        email=email,
        details=f"plan={plan}, billing={billing}",
    )

    # --- TEST MODE: generate license directly ---
    # In production, this block would be replaced by Stripe/PayPal session creation.
    if IS_DEV or not stripe.api_key:
        key = generate_key(tier, billing_code)
        expires_at = compute_expiry(billing_code)

        db = get_db()
        db.execute(
            """INSERT INTO licenses
               (license_key, tier, billing, email, nom, prenom, expires_at,
                payment_provider, payment_id, is_test)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (key, tier, billing_code, email, nom, prenom, expires_at,
             "test", f"test_{secrets.token_hex(8)}", 1),
        )
        db.commit()

        log_transaction(
            "license_generated_test",
            "test",
            email=email,
            details=f"key={key}, tier={tier}",
        )

        send_license_email(email, nom, prenom, key, tier, expires_at)

        logger.info("TEST license generated: %s for %s", key, email)
        return jsonify({
            "mode": "test",
            "license_key": key,
            "tier": plan,
            "billing": billing,
            "expires_at": expires_at,
            "message": "Test license generated (dev mode).",
        }), 201

    # --- STRIPE CHECKOUT ---
    try:
        price_map = {
            ("E", "M"): os.getenv("STRIPE_PRICE_ESSENTIAL_MONTHLY", ""),
            ("E", "A"): os.getenv("STRIPE_PRICE_ESSENTIAL_ANNUAL", ""),
            ("P", "M"): os.getenv("STRIPE_PRICE_PRO_MONTHLY", ""),
            ("P", "A"): os.getenv("STRIPE_PRICE_PRO_ANNUAL", ""),
            ("S", "M"): os.getenv("STRIPE_PRICE_STUDIO_MONTHLY", ""),
            ("S", "A"): os.getenv("STRIPE_PRICE_STUDIO_ANNUAL", ""),
        }
        price_id = price_map.get((tier, billing_code), "")
        if not price_id:
            return jsonify({"error": "Price configuration missing for this plan"}), 500

        session = stripe.checkout.Session.create(
            payment_method_types=["card"],
            line_items=[{"price": price_id, "quantity": 1}],
            mode="payment" if billing_code == "A" else "subscription",
            customer_email=email,
            metadata={
                "tier": tier,
                "billing": billing_code,
                "nom": nom,
                "prenom": prenom,
            },
            success_url=os.getenv("STRIPE_SUCCESS_URL", "https://beatmate.app/success?session_id={CHECKOUT_SESSION_ID}"),
            cancel_url=os.getenv("STRIPE_CANCEL_URL", "https://beatmate.app/cancel"),
        )

        log_transaction(
            "stripe_checkout_created",
            "stripe",
            email=email,
            payment_id=session.id,
        )

        return jsonify({"checkout_url": session.url}), 200

    except stripe.error.StripeError as e:
        logger.exception("Stripe error during checkout creation")
        return jsonify({"error": "Payment service error"}), 502


# --- Stripe webhook ---
@app.route("/api/webhook/stripe", methods=["POST"])
@limiter.limit("10 per minute")
def webhook_stripe():
    payload = request.get_data()
    sig_header = request.headers.get("Stripe-Signature", "")

    if not STRIPE_WEBHOOK_SECRET:
        logger.error("Stripe webhook secret not configured")
        abort(500)

    # Verify signature
    try:
        event = stripe.Webhook.construct_event(
            payload, sig_header, STRIPE_WEBHOOK_SECRET
        )
    except ValueError:
        logger.warning("Stripe webhook: invalid payload from %s", request.remote_addr)
        abort(400)
    except stripe.error.SignatureVerificationError:
        logger.warning("Stripe webhook: invalid signature from %s", request.remote_addr)
        abort(400)

    logger.info("Stripe webhook event: %s", event["type"])

    if event["type"] == "checkout.session.completed":
        session = event["data"]["object"]
        metadata = session.get("metadata", {})

        tier = metadata.get("tier", "E")
        billing_code = metadata.get("billing", "A")
        email = session.get("customer_email", "")
        nom = metadata.get("nom", "")
        prenom = metadata.get("prenom", "")
        payment_id = session.get("payment_intent", session.get("id", ""))

        if not email:
            logger.error("Stripe webhook: no email in session %s", session.get("id"))
            return jsonify({"error": "No email"}), 400

        key = generate_key(tier, billing_code)
        expires_at = compute_expiry(billing_code)

        db = get_db()
        db.execute(
            """INSERT INTO licenses
               (license_key, tier, billing, email, nom, prenom, expires_at,
                payment_provider, payment_id, is_test)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (key, tier, billing_code, email, nom, prenom, expires_at,
             "stripe", str(payment_id), 0),
        )
        db.commit()

        log_transaction(
            "license_generated",
            "stripe",
            email=email,
            payment_id=str(payment_id),
            details=f"key={key}, tier={tier}",
        )

        send_license_email(email, nom, prenom, key, tier, expires_at)
        logger.info("License generated via Stripe: %s for %s", key, email)

    elif event["type"] == "payment_intent.payment_failed":
        intent = event["data"]["object"]
        logger.warning(
            "Stripe payment failed: %s",
            intent.get("last_payment_error", {}).get("message", "unknown"),
        )
        log_transaction(
            "payment_failed",
            "stripe",
            payment_id=intent.get("id"),
            details=intent.get("last_payment_error", {}).get("message", ""),
        )

    return jsonify({"received": True}), 200


# --- PayPal webhook ---
@app.route("/api/webhook/paypal", methods=["POST"])
@limiter.limit("10 per minute")
def webhook_paypal():
    # Verify PayPal webhook authenticity
    # PayPal sends a webhook ID in headers for verification
    transmission_id = request.headers.get("PAYPAL-TRANSMISSION-ID", "")
    transmission_time = request.headers.get("PAYPAL-TRANSMISSION-TIME", "")
    cert_url = request.headers.get("PAYPAL-CERT-URL", "")
    auth_algo = request.headers.get("PAYPAL-AUTH-ALGO", "")
    transmission_sig = request.headers.get("PAYPAL-TRANSMISSION-SIG", "")

    if not all([transmission_id, transmission_time, transmission_sig]):
        logger.warning("PayPal webhook: missing verification headers from %s", request.remote_addr)
        abort(400)

    # In production, verify webhook signature via PayPal API
    # https://developer.paypal.com/docs/api/webhooks/v1/#verify-webhook-signature_post
    webhook_id = os.getenv("PAYPAL_WEBHOOK_ID", "")

    if not IS_DEV and webhook_id:
        import urllib.request
        import json as json_mod

        # Get PayPal access token
        try:
            auth_string = f"{PAYPAL_CLIENT_ID}:{PAYPAL_CLIENT_SECRET}"
            import base64
            auth_b64 = base64.b64encode(auth_string.encode()).decode()

            token_req = urllib.request.Request(
                f"{PAYPAL_API_BASE}/v1/oauth2/token",
                data=b"grant_type=client_credentials",
                headers={
                    "Authorization": f"Basic {auth_b64}",
                    "Content-Type": "application/x-www-form-urlencoded",
                },
            )
            with urllib.request.urlopen(token_req, timeout=10) as resp:
                token_data = json_mod.loads(resp.read())
            access_token = token_data["access_token"]

            # Verify webhook signature
            verify_payload = json_mod.dumps({
                "auth_algo": auth_algo,
                "cert_url": cert_url,
                "transmission_id": transmission_id,
                "transmission_sig": transmission_sig,
                "transmission_time": transmission_time,
                "webhook_id": webhook_id,
                "webhook_event": request.get_json(silent=True),
            }).encode()

            verify_req = urllib.request.Request(
                f"{PAYPAL_API_BASE}/v1/notifications/verify-webhook-signature",
                data=verify_payload,
                headers={
                    "Authorization": f"Bearer {access_token}",
                    "Content-Type": "application/json",
                },
            )
            with urllib.request.urlopen(verify_req, timeout=10) as resp:
                verify_data = json_mod.loads(resp.read())

            if verify_data.get("verification_status") != "SUCCESS":
                logger.warning("PayPal webhook: signature verification failed from %s", request.remote_addr)
                abort(403)

        except Exception:
            logger.exception("PayPal webhook verification error")
            abort(500)

    data = request.get_json(silent=True)
    if not data:
        abort(400)

    event_type = data.get("event_type", "")
    logger.info("PayPal webhook event: %s", event_type)

    if event_type == "PAYMENT.CAPTURE.COMPLETED":
        resource = data.get("resource", {})
        custom_id = resource.get("custom_id", "")
        payment_id = resource.get("id", "")

        # custom_id should contain: tier|billing|email|nom|prenom
        parts = custom_id.split("|") if custom_id else []
        if len(parts) < 5:
            logger.error("PayPal webhook: invalid custom_id: %s", custom_id)
            return jsonify({"error": "Invalid custom_id metadata"}), 400

        tier = sanitize(parts[0], 1).upper()
        billing_code = sanitize(parts[1], 1).upper()
        email = sanitize(parts[2], 254)
        nom = sanitize(parts[3], 100)
        prenom = sanitize(parts[4], 100)

        if tier not in ("E", "P", "S"):
            tier = "E"
        if billing_code not in ("A", "M"):
            billing_code = "A"
        if not validate_email(email):
            logger.error("PayPal webhook: invalid email in custom_id")
            return jsonify({"error": "Invalid email in metadata"}), 400

        key = generate_key(tier, billing_code)
        expires_at = compute_expiry(billing_code)

        db = get_db()
        db.execute(
            """INSERT INTO licenses
               (license_key, tier, billing, email, nom, prenom, expires_at,
                payment_provider, payment_id, is_test)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (key, tier, billing_code, email, nom, prenom, expires_at,
             "paypal", str(payment_id), 0),
        )
        db.commit()

        log_transaction(
            "license_generated",
            "paypal",
            email=email,
            payment_id=str(payment_id),
            details=f"key={key}, tier={tier}",
        )

        send_license_email(email, nom, prenom, key, tier, expires_at)
        logger.info("License generated via PayPal: %s for %s", key, email)

    return jsonify({"received": True}), 200


# --- Validate license ---
@app.route("/api/validate-license", methods=["POST"])
@limiter.limit("30 per minute")
@require_json("key")
def validate_license():
    data = request.get_json()

    key = sanitize(data["key"], 23).upper()
    hwid = sanitize(data.get("hwid", ""), 128)
    mac_address = sanitize(data.get("mac_address", ""), 17)

    # Format validation
    if not validate_license_key(key):
        _log_validation(key, hwid, "invalid_format")
        return jsonify({"valid": False, "error": "Invalid key format"}), 400

    # Luhn validation
    digits = "".join(c for c in key if c.isalnum())
    if not luhn_check(digits):
        _log_validation(key, hwid, "luhn_failed")
        return jsonify({"valid": False, "error": "Invalid key checksum"}), 400

    if hwid and not validate_hwid(hwid):
        return jsonify({"valid": False, "error": "Invalid HWID format"}), 400

    if mac_address and not validate_mac(mac_address):
        return jsonify({"valid": False, "error": "Invalid MAC address format"}), 400

    # Database lookup
    db = get_db()
    row = db.execute(
        "SELECT * FROM licenses WHERE license_key = ?", (key,)
    ).fetchone()

    if row is None:
        _log_validation(key, hwid, "not_found")
        return jsonify({"valid": False, "error": "License key not found"}), 404

    # Check expiry
    if row["expires_at"]:
        expires = datetime.strptime(row["expires_at"], "%Y-%m-%d %H:%M:%S").replace(
            tzinfo=timezone.utc
        )
        if datetime.now(timezone.utc) > expires:
            _log_validation(key, hwid, "expired")
            return jsonify({"valid": False, "error": "License has expired"}), 403

    # HWID binding
    if row["hwid"] is None and hwid:
        # First activation: bind to this HWID
        db.execute(
            "UPDATE licenses SET hwid = ?, mac_address = ?, activated = 1 WHERE license_key = ?",
            (hwid, mac_address or None, key),
        )
        db.commit()
        _log_validation(key, hwid, "activated")
        logger.info("License %s activated with HWID %s", key, hwid)
    elif row["hwid"] and hwid and row["hwid"] != hwid:
        # HWID mismatch
        _log_validation(key, hwid, "hwid_mismatch")
        logger.warning(
            "HWID mismatch for license %s: expected=%s got=%s",
            key, row["hwid"], hwid,
        )
        return jsonify({"valid": False, "error": "License is bound to a different machine"}), 403

    tier_names = {"E": "essential", "P": "pro", "S": "studio"}
    _log_validation(key, hwid, "valid")

    return jsonify({
        "valid": True,
        "type": tier_names.get(row["tier"], "essential"),
        "tier": row["tier"],
        "billing": row["billing"],
        "expires_at": row["expires_at"],
        "activated": bool(row["activated"]),
    }), 200


def _log_validation(key: str, hwid: str, result: str):
    """Log a validation attempt."""
    try:
        db = get_db()
        db.execute(
            """INSERT INTO validation_log
               (license_key, hwid, ip_address, user_agent, result)
               VALUES (?, ?, ?, ?, ?)""",
            (
                key[:23],
                hwid[:128] if hwid else None,
                request.remote_addr,
                request.headers.get("User-Agent", "")[:500],
                result,
            ),
        )
        db.commit()
    except Exception:
        logger.exception("Failed to log validation")


# --- License info ---
@app.route("/api/license-info/<key>", methods=["GET"])
@limiter.limit("30 per minute")
def license_info(key):
    key = sanitize(key, 23).upper()

    if not validate_license_key(key):
        return jsonify({"error": "Invalid key format"}), 400

    db = get_db()
    row = db.execute(
        "SELECT tier, billing, activated, created_at, expires_at FROM licenses WHERE license_key = ?",
        (key,),
    ).fetchone()

    if row is None:
        return jsonify({"error": "License not found"}), 404

    tier_names = {"E": "essential", "P": "pro", "S": "studio"}
    billing_names = {"A": "annual", "M": "monthly"}

    return jsonify({
        "type": tier_names.get(row["tier"], "essential"),
        "billing": billing_names.get(row["billing"], "annual"),
        "activated": bool(row["activated"]),
        "created_at": row["created_at"],
        "expires_at": row["expires_at"],
    }), 200


# --- Test generate (dev only) ---
@app.route("/api/test-generate", methods=["POST"])
@limiter.limit("5 per minute")
def test_generate():
    if not IS_DEV:
        abort(404)

    data = request.get_json(silent=True) or {}

    plan = sanitize(data.get("plan", "essential")).lower()
    billing = sanitize(data.get("billing", "annual")).lower()
    email = sanitize(data.get("email", "test@beatmate.app"), 254)
    nom = sanitize(data.get("nom", "Test"), 100)
    prenom = sanitize(data.get("prenom", "User"), 100)

    if plan not in VALID_PLANS:
        plan = "essential"
    if billing not in VALID_BILLING:
        billing = "annual"

    tier = PLAN_TIER_MAP[plan]
    billing_code = BILLING_CODE_MAP[billing]

    key = generate_key(tier, billing_code)
    expires_at = compute_expiry(billing_code)

    db = get_db()
    db.execute(
        """INSERT INTO licenses
           (license_key, tier, billing, email, nom, prenom, expires_at,
            payment_provider, payment_id, is_test)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (key, tier, billing_code, email, nom, prenom, expires_at,
         "test", f"test_{secrets.token_hex(8)}", 1),
    )
    db.commit()

    log_transaction(
        "test_generate",
        "test",
        email=email,
        details=f"key={key}, tier={tier}, billing={billing_code}",
    )

    logger.info("TEST license generated: %s (tier=%s) for %s", key, tier, email)

    return jsonify({
        "license_key": key,
        "tier": plan,
        "billing": billing,
        "email": email,
        "expires_at": expires_at,
        "message": "Test license generated successfully.",
    }), 201


# ---------------------------------------------------------------------------
# Error handlers
# ---------------------------------------------------------------------------
@app.errorhandler(400)
def bad_request(e):
    return jsonify({"error": "Bad request"}), 400


@app.errorhandler(403)
def forbidden(e):
    return jsonify({"error": "Forbidden"}), 403


@app.errorhandler(404)
def not_found(e):
    return jsonify({"error": "Not found"}), 404


@app.errorhandler(429)
def rate_limited(e):
    return jsonify({"error": "Too many requests. Please try again later."}), 429


@app.errorhandler(500)
def internal_error(e):
    logger.exception("Internal server error")
    return jsonify({"error": "Internal server error"}), 500


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    logger.info("Starting BeatMate V11 License Server (dev=%s)", IS_DEV)
    app.run(
        host="127.0.0.1",
        port=5000,
        debug=IS_DEV,
    )
