`timescale 1ns/1ps
module color_detector_axis_tb;
    localparam WIDTH=8;
    reg clk=0; always #5 clk=~clk;
    reg rst=0, valid=0, last=0, userbit=0, ready=0;
    reg [23:0] data=0;
    wire sr,mv,ml,mu;
    wire [23:0] md;
    color_detector_axis #(.FRAME_WIDTH(WIDTH)) dut
        (clk,rst,data,valid,sr,last,userbit,md,mv,ready,ml,mu);

    reg [25:0] expected[0:8191];
    reg [23:0] reference_row[0:WIDTH-1];
    reg [23:0] reference_left, reference_value;
    reg [7:0] horizontal,vertical;
    integer x=0,y=0,written=0,read_count=0,cycles=0,stalls=0,frames=0;
    integer directed_count=0,verified_count=0,before_continuous=0;
    reg stalled=0;
    reg [25:0] held;
    reg explicit_check=0;
    reg [23:0] explicit_expected=0;
    reg random_ready=0;
    reg [31:0] lfsr=32'h1234abcd;

    function [7:0] reference_difference;
        input [23:0] a,b;
        integer i,delta,maximum;
        begin
            maximum=0;
            for(i=0;i<3;i=i+1) begin
                delta=((a>>(8*i))&255)-((b>>(8*i))&255);
                if(delta<0) delta=-delta;
                if(delta>maximum) maximum=delta;
            end
            reference_difference=maximum;
        end
    endfunction

    always @(negedge clk) begin
        lfsr <= {lfsr[30:0],lfsr[31]^lfsr[21]^lfsr[1]^lfsr[0]};
        if(random_ready) ready <= lfsr[0] | lfsr[4];
    end
    always @(posedge clk) begin
        cycles=cycles+1;
        if(cycles>20000) $fatal(1,"watchdog timeout");
        if(!rst) begin
            x=0; y=0; written=0; read_count=0; stalled=0; reference_left=0;
        end else begin
            if(stalled && (!mv || {mu,ml,md} !== held))
                $fatal(1,"output changed under backpressure");
            if(valid && sr) begin
                horizontal=(x==0) ? 255 : reference_difference(data,reference_left);
                vertical=(y==0) ? 255 : reference_difference(data,reference_row[x]);
                reference_value={8'b0,vertical,horizontal};
                if(explicit_check && reference_value !== explicit_expected)
                    $fatal(1,"directed expectation mismatch at (%0d,%0d): got %h expected %h",
                           x,y,reference_value,explicit_expected);
                expected[written]={userbit,last,reference_value};
                written=written+1;
                reference_row[x]=data;
                reference_left=data;
                if(last) begin x=0; y=0; frames=frames+1; end
                else if(x==WIDTH-1) begin x=0; y=y+1; end
                else x=x+1;
            end
            if(mv && ready) begin
                if(read_count>=written || {mu,ml,md} !== expected[read_count])
                    $fatal(1,"data/sideband mismatch at pixel %0d: got %h expected %h",
                           read_count,{mu,ml,md},expected[read_count]);
                read_count=read_count+1;
            end
            stalled=mv && !ready;
            held={mu,ml,md};
            if(stalled) stalls=stalls+1;
        end
    end
    task send;
        input [23:0] pixel;
        input tl,tu;
        begin
            @(negedge clk); data=pixel; last=tl; userbit=tu; valid=1;
            @(posedge clk); while(!sr) @(posedge clk);
            @(negedge clk); valid=0;
        end
    endtask
    task check;
        input [7:0] r,g,b,h,v;
        input tl,tu;
        begin
            explicit_check=1; explicit_expected={8'b0,v,h};
            send({b,g,r},tl,tu);
            explicit_check=0; directed_count=directed_count+1;
        end
    endtask
    integer i;
    initial begin
        repeat(3) @(negedge clk);
        rst=1; ready=1;
        // Two complete rows with explicit differences. Any RGB value is valid:
        // arbitrary shades, cyan, magenta and grayscale have no class limits.
        check(10,20,30,255,255,0,1);
        check(10,20,30,0,255,0,0);
        check(50,20,30,40,255,0,0);
        check(50,100,30,80,255,0,0);
        check(50,100,255,225,255,0,1);
        check(255,0,255,205,255,0,0);
        check(0,255,255,255,255,0,0);
        check(128,128,128,128,255,0,0);
        check(10,20,30,255,0,0,1);
        check(20,20,30,10,10,0,0);
        check(20,20,30,0,30,0,0);
        check(20,20,30,0,80,0,0);
        check(20,20,30,0,225,0,1);
        check(255,0,255,235,0,0,0);
        check(0,255,255,255,0,0,0);
        check(128,128,128,128,0,1,1);
        // A new frame must ignore all previous-frame memory.
        check(77,91,123,255,255,1,1);
        check(77,91,123,255,255,1,0);

        repeat(3) @(negedge clk);
        ready=0;
        send(24'h123456,1,1);
        repeat(12) @(negedge clk);
        ready=1;
        repeat(4) @(negedge clk);

        // Continuous input: one accepted pixel per clock, frame boundary
        // crossed without a bubble; previous-row reads and writes overlap.
        before_continuous=written;
        valid=1;
        for(i=0;i<128;i=i+1) begin
            data=(i*32'h13579b)&24'hffffff;
            last=(i%64)==63; userbit=(i%64)==0;
            @(negedge clk);
        end
        valid=0;
        if(written-before_continuous!=128) $fatal(1,"continuous throughput failed");

        random_ready=1;
        for(i=0;i<1024;i=i+1)
            send($random,(i%64)==63,(i%11)==0);
        // Dense random stream. Hold both data and sidebands until accepted.
        @(negedge clk); valid=1;
        for(i=0;i<1536;i=i+1) begin
            data=$random; last=(i%48)==47; userbit=(i%13)==0;
            @(posedge clk); while(!sr) @(posedge clk);
            @(negedge clk);
        end
        valid=0; random_ready=0;
        @(negedge clk); ready=1;
        repeat(5) @(negedge clk);
        if(read_count!=written || written!=directed_count+2689 || stalls<12)
            $fatal(1,"count/stall failure sent=%0d got=%0d stalls=%0d",written,read_count,stalls);
        verified_count=written;

        // Fill both stages, stall, then reset: both pending pixels are dropped.
        ready=0;
        @(negedge clk); valid=1; data=24'h876543; last=0; userbit=1;
        @(negedge clk); data=24'habcdef; last=1; userbit=0;
        @(negedge clk); valid=0;
        repeat(3) @(negedge clk);
        rst=0;
        repeat(2) @(negedge clk);
        if(mv || sr) $fatal(1,"reset failed");
        rst=1; ready=1;
        check(1,2,3,255,255,0,1);
        check(1,2,3,0,255,1,0); // partial row TLAST also starts a fresh frame
        check(1,2,3,255,255,1,1);
        repeat(5) @(negedge clk);
        if(read_count!=3) $fatal(1,"post-reset transfers failed");
        $display("PASS: colors unrestricted RGB neighbor differences, %0d explicit cases, %0d transfers before reset, %0d frames, %0d stalled cycles, row/frame boundaries, continuous traffic, TLAST/TUSER, reset",
                 directed_count,verified_count,frames,stalls);
        $finish;
    end
endmodule
