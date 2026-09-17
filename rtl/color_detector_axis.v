`timescale 1ns/1ps
// RGB input byte order R,G,B. The legacy module name keeps BD integration simple.
// Output byte 0: max absolute RGB difference from the pixel immediately left.
// Output byte 1: max absolute RGB difference from the pixel immediately above.
// Output byte 2: zero. First-column/first-row differences are 255.
// These generic similarity features do not impose a fixed set of color classes.
module color_detector_axis #(
    parameter integer FRAME_WIDTH = 640
)(
    input wire aclk, input wire aresetn,
    input wire [23:0] s_axis_tdata,
    input wire s_axis_tvalid, output wire s_axis_tready,
    input wire s_axis_tlast, input wire s_axis_tuser,
    output reg [23:0] m_axis_tdata,
    output reg m_axis_tvalid, input wire m_axis_tready,
    output reg m_axis_tlast, output reg m_axis_tuser
);
    localparam integer COLUMN_BITS = FRAME_WIDTH > 1 ? $clog2(FRAME_WIDTH) : 1;
    reg [COLUMN_BITS-1:0] column=0;
    reg first_row=1'b1;
    reg [23:0] previous_pixel;

    // A read-first synchronous row memory stores one complete previous row.
    // No RAM reset is needed: first-row flags hide all stale memory contents.
    (* ram_style = "block" *) reg [23:0] row_memory [0:FRAME_WIDTH-1];
    reg [23:0] above_pixel;
    reg [23:0] pixel_stage1, left_stage1;
    reg first_column_stage1, first_row_stage1;
    reg valid_stage1, last_stage1, user_stage1;

    // Both pipeline stages freeze together if the output cannot be accepted.
    wire advance = !m_axis_tvalid || m_axis_tready;
    assign s_axis_tready = aresetn && advance;
    always @(posedge aclk) begin
        if(aresetn && advance && s_axis_tvalid) begin
            above_pixel <= row_memory[column];
            row_memory[column] <= s_axis_tdata;
        end
    end

    function [7:0] max_rgb_difference;
        input [23:0] a,b;
        reg [7:0] dr,dg,db,maximum;
        begin
            dr = a[7:0] >= b[7:0] ? a[7:0]-b[7:0] : b[7:0]-a[7:0];
            dg = a[15:8] >= b[15:8] ? a[15:8]-b[15:8] : b[15:8]-a[15:8];
            db = a[23:16] >= b[23:16] ? a[23:16]-b[23:16] : b[23:16]-a[23:16];
            maximum = dr >= dg ? dr : dg;
            max_rgb_difference = maximum >= db ? maximum : db;
        end
    endfunction

    always @(posedge aclk) begin
        if(!aresetn) begin
            column <= 0;
            first_row <= 1'b1;
            previous_pixel <= 0;
            valid_stage1 <= 1'b0;
            pixel_stage1 <= 0;
            left_stage1 <= 0;
            first_column_stage1 <= 1'b1;
            first_row_stage1 <= 1'b1;
            last_stage1 <= 1'b0;
            user_stage1 <= 1'b0;
            m_axis_tvalid <= 1'b0;
            m_axis_tdata <= 0;
            m_axis_tlast <= 1'b0;
            m_axis_tuser <= 1'b0;
        end else if(advance) begin
            m_axis_tvalid <= valid_stage1;
            if(valid_stage1) begin
                m_axis_tdata <= {8'b0,
                    first_row_stage1 ? 8'd255 : max_rgb_difference(pixel_stage1,above_pixel),
                    first_column_stage1 ? 8'd255 : max_rgb_difference(pixel_stage1,left_stage1)};
                m_axis_tlast <= last_stage1;
                m_axis_tuser <= user_stage1;
            end
            valid_stage1 <= s_axis_tvalid;
            if(s_axis_tvalid) begin
                pixel_stage1 <= s_axis_tdata;
                left_stage1 <= previous_pixel;
                first_column_stage1 <= (column == 0);
                first_row_stage1 <= first_row;
                last_stage1 <= s_axis_tlast;
                user_stage1 <= s_axis_tuser;
                previous_pixel <= s_axis_tdata;
                // TLAST ends a frame, not a row. TUSER is transported unchanged.
                if(s_axis_tlast) begin
                    column <= 0;
                    first_row <= 1'b1;
                end else if(column == FRAME_WIDTH-1) begin
                    column <= 0;
                    first_row <= 1'b0;
                end else column <= column + 1'b1;
            end
        end
    end
endmodule
