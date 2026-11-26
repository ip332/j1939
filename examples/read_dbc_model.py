#!/usr/bin/env python3
"""Minimal host consumer using generated protobuf bindings, without parsing DBC.

Generate dbc_model_v1_pb2.py from schema/dbc_model_v1.proto with protoc and add
its directory to PYTHONPATH. Usage: read_dbc_model.py model.textproto --dbc-id 100
This demonstrates selection, not BRD generation or a complete model validator.
"""
import argparse
from google.protobuf import text_format
import dbc_model_v1_pb2


def load_model(path):
    with open(path, encoding="ascii") as source:
        model = text_format.Parse(source.read(), dbc_model_v1_pb2.Model())
    if (not model.IsInitialized() or model.format != "j1939.dbc-model"
            or model.version != 1):
        raise ValueError("unsupported or incomplete DBC model format/version")
    return model


def select_message(model, dbc_id):
    matches = [message for message in model.messages if message.dbc_id == dbc_id]
    if len(matches) != 1:
        raise ValueError("selection must identify exactly one original DBC message")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model")
    parser.add_argument("--dbc-id", required=True, type=lambda text: int(text, 0))
    args = parser.parse_args()
    try:
        message = select_message(load_model(args.model), args.dbc_id)
    except (ValueError, OSError, text_format.ParseError) as error:
        parser.exit(1, str(error) + "\n")
    print(text_format.MessageToString(message), end="")


if __name__ == "__main__":
    main()
