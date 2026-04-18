with Ada.Command_Line;
with Hesia_Sentinel_Core;

procedure Hesia_Sentinel_Kernel is
   Status : constant Integer := Hesia_Sentinel_Core.Check;
begin
   Ada.Command_Line.Set_Exit_Status (Status);
end Hesia_Sentinel_Kernel;
