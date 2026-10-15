# Gotchi Meet

Your Google Meet meetings, audio only, on a CGotchi. The Gotchi lists your upcoming meetings,
joins each one when it starts, plays the meeting through its speaker and sends your voice from its
mic. There is no screen or video.

```
Google Calendar <-- meet server (Vercel) --> Recall.ai bot "CGotchi" in the Meet
                        |                         | its web page (public/bot.html)
Gotchi --- LiveKit room +-------------------------+ meeting audio <-> Gotchi audio
```

The bot's web page is a LiveKit participant. Its microphone is the meeting audio, which it sends
to the Gotchi, and whatever it plays (the Gotchi's mic) goes into the meeting. The Gotchi side is
the same LiveKit audio as Gotchi Call (`call/firmware`), whose board, display and audio code it
reuses.

## Gotchi controls

- **List:** Vol+ / Vol- move the selection, the middle key joins the selected meeting now. A
  meeting is joined automatically at its start time.
- **In a meeting:** you start **muted**. Vol+ toggles the mic (red circle = muted, green = live).
  Vol- steps the speaker volume. The middle key leaves.
- Hold the middle key for 2 s to power off.

The bot joins as a guest named "CGotchi", so the host has to let it in. The Gotchi shows
"Waiting to be let in" until then and gives up after 10 minutes. When the meeting ends the bot
leaves and the Gotchi goes back to the list.

## 1. Accounts

- **Recall.ai:** sign up at recall.ai and create an API key. Note the region of your dashboard
  (for example `us-west-2`). Each meeting runs a `web_4_core` bot, which costs more per hour than
  Recall's default bot because it runs a browser with WebRTC.
- **LiveKit:** the same LiveKit Cloud project as Gotchi Call works.
- **Google:** in Google Cloud Console, enable the Google Calendar API, set up the OAuth consent
  screen (External, add yourself as a test user) and create an OAuth client of type "Web
  application" with the redirect URI `https://<your-server>/api/google/callback`.

## 2. Meet server

Deploy `server/` to Vercel as its own project (`vercel deploy` from that folder) and set:

| Variable | Value |
|---|---|
| `LIVEKIT_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET` | From the LiveKit project settings |
| `RECALL_API_KEY` | Your Recall.ai API key |
| `RECALL_REGION` | Your Recall.ai region, default `us-west-2` |
| `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET` | The OAuth client from step 1 |
| `GOOGLE_REFRESH_TOKEN` | From the sign-in below |
| `DEVICE_SECRET` | Any long random string; the Gotchi sends it |
| `PUBLIC_URL` | Optional: the server's public URL, if not its Vercel host name |
| `BOT_NAME` | Optional: the bot's name in the meeting, default `CGotchi` |

Then open `https://<your-server>/api/google/login` once, sign in, copy the refresh token it shows
into `GOOGLE_REFRESH_TOKEN` and redeploy.

Endpoints, all with `Authorization: Bearer DEVICE_SECRET`: `GET /api/meetings`,
`POST /api/meet/join` (`{"eventId"}`), `POST /api/meet/leave` (`{"meetId", "botId"}`).

## 3. Gotchi firmware

Needs ESP-IDF 5.4 or 5.5, like Gotchi Call.

```sh
cd meet/firmware
cp secrets.defaults.example secrets.defaults   # Wi-Fi, server URL, DEVICE_SECRET
./build.sh build flash monitor
```

This overwrites MicroPixel. To go back, flash your MicroPixel backup or rebuild MicroPixel.
