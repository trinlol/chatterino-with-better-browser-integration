# Browser Integration Context

This project connects Twitch's browser experience to a Chatterino desktop client while preserving Twitch-native interactive controls.

## Language

**Companion**:
The browser extension that observes Twitch and coordinates the browser-side experience.
_Avoid_: Plugin, overlay

**Native Host**:
The Chatterino process reached through the browser native-messaging transport.
_Avoid_: Server, backend

**Activity**:
A channel-scoped Twitch interaction such as a **Poll** or **Prediction**.
_Avoid_: Banner, event

**Poll**:
An **Activity** whose choices are decided by viewer votes.
_Avoid_: Prediction

**Prediction**:
An **Activity** whose outcomes are decided after the predicted event.
_Avoid_: Poll

**Voting Surface**:
The Twitch-native or Companion-replicated control that lets a viewer participate in an **Activity**.
_Avoid_: Banner, announcement

**Activity Adapter**:
A browser-side source that converts Twitch DOM or GraphQL state into the normalized **Activity** Interface.
_Avoid_: Scraper

**Integration Health**:
The observable connection and synchronization state between the **Companion**, Twitch, and the **Native Host**.
_Avoid_: Debug status

## Relationships

- A Twitch channel has zero or one active **Poll** and zero or one active **Prediction**
- A **Companion** observes **Activities** through one or more **Activity Adapters**
- A **Voting Surface** belongs to exactly one **Activity**
- A **Companion** sends normalized **Activities** to the **Native Host**
- **Integration Health** describes both transport state and the most recent channel synchronization

## Example dialogue

> **Dev:** "The DOM Adapter lost the Prediction banner. Should it remove the channel's Poll too?"
> **Domain expert:** "No. Polls and Predictions are separate Activities; remove only the Activity that disappeared, and keep its Voting Surface available while Twitch still exposes it."

## Flagged ambiguities

- "prediction" previously named the combined desktop engagement banner; resolved: **Activity** is the shared concept and **Prediction** is one Activity kind.
- "overlay" was used for both the entire **Companion** and an injected chat frame; resolved: **Companion** names the extension and overlay is reserved for the chat-frame Implementation.
