# Gotchi Meet

Your Google Meet meetings, audio only, on a CGotchi. The Gotchi lists your upcoming meetings,
joins each one when it starts, plays the meeting through its speaker and sends your voice from its
mic. There is no screen or video.

```
Google Calendar <-- call server (Vercel) --> Recall.ai bot "CGotchi" in the Meet
                        |                         | its web page (public/bot.html)
Gotchi --- LiveKit room +-------------------------+ meeting audio <-> Gotchi audio
```

The bot's web page is a LiveKit participant. Its microphone is the meeting audio, which it sends
to the Gotchi, and whatever it plays (the Gotchi's mic) goes into the meeting. On the Gotchi it is
an ordinary call on MicroPixel's Call service, so the audio is the same as Gotchi Call.

## Gotchi controls

- **List:** VOL- / VOL+ move the selection; BOOT or a tap joins the selected meeting now. A
  meeting is joined automatically at its start time.
- **In a meeting:** you start **muted**. VOL+ toggles the mic (red "Muted", green "Live"). Hold
  VOL- for a second (or press BOOT) to leave. Taps do nothing during a meeting, so a stray touch
  can't drop you.

The bot joins as a guest named "CGotchi", so the host has to let it in. The Gotchi shows
"Waiting... to be let in" until then and gives up after 10 minutes. When the meeting ends the bot
leaves and the Gotchi goes back to the list.

## 1. Accounts

- **Recall.ai:** sign up at recall.ai and create an API key. Note the region of your dashboard
  (for example `us-west-2`). Each meeting runs a `web_4_core` bot, which costs more per hour than
  Recall's default bot because it runs a browser with WebRTC.
- **LiveKit:** the same LiveKit Cloud project as Gotchi Call works.
- **Google:** in Google Cloud Console, enable the Google Calendar API, set up the OAuth consent
  screen (External, add yourself as a test user) and create an OAuth client of type "Web
  application" with the redirect URI `https://<your-server>/api/google/callback`.

## 2. Call server

The Meet routes live in the Gotchi Call server (`call/server`), so deploy that (see
`call/README.md`) and add these variables:

| Variable | Value |
|---|---|
| `RECALL_API_KEY` | Your Recall.ai API key |
| `RECALL_REGION` | Your Recall.ai region, default `us-west-2` |
| `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET` | The OAuth client from step 1 |
| `GOOGLE_REFRESH_TOKEN` | From the sign-in below |
| `PUBLIC_URL` | Optional: the server's public URL, if not its Vercel host name |
| `BOT_NAME` | Optional: the bot's name in the meeting, default `CGotchi` |

Then open `https://<your-server>/api/google/login` once, sign in, copy the refresh token it shows
into `GOOGLE_REFRESH_TOKEN` and redeploy.

The Gotchi uses `GET /api/meetings` for the list, then the normal call endpoints:
`POST /api/call` with `{"to": "meet:<key>"}` sends the bot in, and `POST /api/call/end` takes it
out. All with `Authorization: Bearer DEVICE_SECRET`.

## 3. Gotchi app

The Meet app (`guest/apps/meet`) and the Call 1.1 service changes it needs (meeting list, mute,
"meet:" calls) are on branch `gotchi-meet-micropixel`, stacked on `gotchi-call-micropixel`. Set
`MICROPIXEL_CALL_SERVER_URL` and `MICROPIXEL_CALL_DEVICE_SECRET` in the repository-root `.env` as for
the Call app, rebuild and flash the host firmware, then install the app.
