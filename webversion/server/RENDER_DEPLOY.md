# Render Deployment Guide for Teleport Signaling Server

## Step-by-Step Instructions

### Step 1: Push to GitHub
First, commit and push the `webversion/server` folder to your GitHub repository.

### Step 2: Create New Web Service on Render
1. Go to [render.com](https://render.com) and sign in
2. Click **"New Web Service"**
3. Connect your GitHub repository

### Step 3: Configure the Service
Use these settings:

| Setting | Value |
|---------|-------|
| **Name** | `teleport-signaling` |
| **Root Directory** | `webversion/server` |
| **Environment** | `Node` |
| **Build Command** | `npm install` |
| **Start Command** | `npm start` |
| **Instance Type** | `Free` |

### Step 4: Deploy
Click **"Create Web Service"** and wait for deployment.

### Step 5: Get Your Server URL
After deployment, Render will give you a URL like:
```
https://teleport-signaling.onrender.com
```

### Step 6: Update Web App
Update the signaling server URL in your web app code to use the Render URL.

---

## Important Notes
- Free tier may spin down after 15 minutes of inactivity
- First request after spin-down takes ~30 seconds
- For production, consider upgrading to a paid tier
