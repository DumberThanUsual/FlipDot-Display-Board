activity.children = [
  {
    type: "inscroll",
    attributes: {
      "index": "0"
    },
    children: [
      {
        type: "text",
        attributes: {
          value: "Text 1",
          x: "1",
          y: "1"
        },
        children: []
      },
      {
        type: "text",
        attributes: {
          value: "Text 2",
          x: "1",
          y: "1"
        },
        children: []
      },
      {
        type: "text",
        attributes: {
          value: "Text 3",
          x: "1",
          y: "1"
        },
        children: []
      }
    ]
  }
];

print("hi");
activity.children[0].attributes.index = "1";
