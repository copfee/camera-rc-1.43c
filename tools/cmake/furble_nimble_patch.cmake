# Port of furble 0cac22a patches/ble_gap.patch (MIT).
# Patch a build-local copy; never modify a shared ESP-IDF checkout.
file(READ "${RC_NIMBLE_SOURCE}" source)
string(REPLACE "\r\n" "\n" source "${source}")
set(original "        rc = ble_store_read_peer_sec(&key_sec, &value_sec);
        if (rc == 0 && value_sec.ltk_present) {")
string(FIND "${source}" "${original}" match)
if(match EQUAL -1)
    message(FATAL_ERROR "Cannot apply furble bond lookup patch to this NimBLE source")
endif()
string(REPLACE "${original}" "" remainder "${source}")
string(LENGTH "${source}" before)
string(LENGTH "${remainder}" after)
string(LENGTH "${original}" removed)
math(EXPR expected "${after} + ${removed}")
if(NOT before EQUAL expected)
    message(FATAL_ERROR "Ambiguous furble bond lookup patch location")
endif()
set(replacement "        rc = ble_store_read_peer_sec(&key_sec, &value_sec);
        if (rc != 0) {
            /* furble: retry the same identity with the other address type. */
            key_sec.peer_addr.type = !key_sec.peer_addr.type;
            rc = ble_store_read_peer_sec(&key_sec, &value_sec);
            BLE_HS_LOG(INFO, \"furble alternate bond lookup: type=%u rc=%d\\n\",
                       key_sec.peer_addr.type, rc);
        }
        if (rc == 0 && value_sec.ltk_present) {")
string(REPLACE "${original}" "${replacement}" patched "${source}")
get_filename_component(output_dir "${RC_NIMBLE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${RC_NIMBLE_OUTPUT}" "${patched}")
